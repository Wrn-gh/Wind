#include "wind/app/Application.h"

#include <shellapi.h>
#include <string>

#include "wind/core/SettingsStore.h"
#include "wind/platform/AutoStart.h"
#include "wind/platform/Dpi.h"
#include "wind/platform/SingleInstance.h"
#include "wind/platform/SystemTheme.h"
#include "wind/platform/Win32Error.h"

namespace wind::app {
namespace {

/// 滚轮单格调整的百分点，比工具栏按钮的步长更细，便于微调。
constexpr int kOpacityWheelStep = 2;

/// 设置文本都是 ASCII 的 "Ctrl+Alt+W"，但仍按 UTF-8 转换，避免以后加中文字符时静默损坏。
std::wstring Widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = static_cast<int>(text.size());
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), length, wide.data(), size);
    return wide;
}

/// @brief 命令行里是否带了 `--quit`（也接受 `/quit`、`-quit`，大小写不敏感）。
///
/// 安装包在覆盖 exe 之前用它请后台实例先正常退出——被强杀的 Wind 来不及还原自己
/// 改过的窗口，用户升级完会发现某些窗口永远是半透明的。
bool HasQuitSwitch() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return false;
    }
    // 允许 --quit / -quit / /quit，大小写不敏感。
    constexpr const wchar_t* kNames[] = {L"--quit", L"-quit", L"/quit"};
    bool found = false;
    for (int i = 1; i < argc && !found; ++i) {
        for (const wchar_t* name : kNames) {
            if (::CompareStringOrdinal(argv[i], -1, name, -1, TRUE) == CSTR_EQUAL) {
                found = true;
                break;
            }
        }
    }
    ::LocalFree(argv);
    return found;
}

}  // namespace

int Application::Run(HINSTANCE instance) {
    instance_ = instance;

    // `Wind.exe --quit`：安装包与脚本用它请后台实例走正常退出流程（会先还原改过的窗口）。
    // 放在单实例检查之前，因为它本来的目的就是让出单实例身份。
    if (HasQuitSwitch()) {
        platform::Log("收到 --quit：请求已有实例退出。");
        return platform::SingleInstance::RequestExistingQuit() ? 0 : 1;
    }

    // 单实例：互斥体抢不到说明已经有 Wind 在跑（它会收到唤醒消息并弹出设置页），
    // 本进程立刻退出，免得第二个实例再注册一遍热键、托盘，互相报“热键已占用”。
    if (!singleInstance_.Acquire()) {
        return 0;
    }

    // 必须在创建任何窗口之前设置，否则叠加层会用系统缩放后的尺寸覆盖虚拟屏幕。
    platform::EnablePerMonitorDpiAwareness();

    if (!overlay_.Create(instance_, this)) {
        ::MessageBoxW(nullptr, L"无法创建叠加层窗口，详细信息见 %TEMP%\\Wind.log。", L"Wind", MB_ICONERROR | MB_OK);
        return 1;
    }

    RECT screen = platform::VirtualScreenBounds();
    if (!renderer_.Initialize(overlay_.Hwnd(), screen.right - screen.left, screen.bottom - screen.top)) {
        ::MessageBoxW(nullptr, L"Direct3D 设备初始化失败，详细信息见 %TEMP%\\Wind.log。", L"Wind",
                      MB_ICONERROR | MB_OK);
        overlay_.Destroy();
        return 1;
    }

    // 设置页窗口只在托盘点「设置…」时才真正用到，但先建好（保持隐藏），
    // 这样菜单项在任何时刻都有窗口可显示，也不需要在此处申请 D3D 设备。
    settingsWindow_.Create(instance_, this);
    settingsWindowReady_ = settingsWindow_.Hwnd() != nullptr;
    if (!settingsWindowReady_) {
        platform::Log("设置页窗口创建失败，托盘里的「设置…」将不可用。");
    }

    SyncUiScale();
    SyncTheme();

    // 图标是编译进 exe 的 RCDATA；成功则工具栏画图标，失败自动退回文字标签。
    icons_.Load(renderer_.Device());
    toolbar_.SetIcons(&icons_);

    // 热键注册全程不弹模态框、不退出：配置键被占用就不注册全局热键，只留托盘入口。
    const HotkeyStatus hotkeyStatus = LoadSettingsAndHotkey();

    // 自愈要在托盘之前做：即使托盘创建失败，移动过位置的 exe 也能把自启项修正过来。
    SyncAutoStart();

    // 托盘失败不影响主功能（热键仍可用），因此只记日志不报错。
    const bool trayReady = tray_.Create(instance_, this);
    if (trayReady) {
        SyncTrayHotkey();
        if (hotkeyStatus == HotkeyStatus::Unavailable) {
            tray_.Notify(L"当前没有全局快捷键",
                         L"配置的快捷键已被其他程序占用，请通过托盘菜单的「设置…」换一个。点击此处修改。", true);
        }

        // Ctrl+Alt+Q（退出）已删除，只在首次启动时提示一次，避免每次开机都弹。
        if (!settings_.quitHotkeyNoticeShown) {
            tray_.Notify(L"退出快捷键已取消",
                         L"为避免与其他程序抢热键，Ctrl+Alt+Q 不再保留；请通过托盘菜单退出。", false);
            settings_.quitHotkeyNoticeShown = true;
            core::SettingsStore::Save(settings_);
        }
    }

    // 既没有热键也没有托盘，程序就彻底没有入口了：这是唯一需要用户当场知道的错误。
    if (!trayReady && hotkeyStatus == HotkeyStatus::Unavailable) {
        ::MessageBoxW(nullptr,
                      L"Wind 无法注册全局快捷键，托盘图标也创建失败，程序没有任何操作入口。\n"
                      L"请检查系统资源后重启，详细信息见 %TEMP%\\Wind.log。",
                      L"Wind", MB_ICONERROR | MB_OK);
    }

    MSG message{};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_HOTKEY) {
            HandleHotkey(static_cast<int>(message.wParam));
            continue;
        }
        // 注销 / 关机时系统只会发 WM_QUERYENDSESSION，走不到下面的正常退出清理，
        // 因此在这里先把改动过的窗口还原掉，再交给默认过程。
        if (message.message == WM_QUERYENDSESSION) {
            RestoreAllWindows();
        }
        // 定时器消息的 hwnd 就是各自的窗口，DispatchMessage 会走到窗口的消息过程；
        // 但重绘必须由应用层驱动，所以先在这里截获。
        if (message.message == WM_TIMER && message.wParam == static_cast<WPARAM>(kFrameTimerId) &&
            message.hwnd == overlay_.Hwnd()) {
            RenderFrame();
            continue;
        }
        if (message.message == WM_TIMER && message.wParam == static_cast<WPARAM>(kSettingsTimerId) &&
            message.hwnd == settingsWindow_.Hwnd()) {
            RenderSettingsFrame();
            continue;
        }
        // 进程里有两个 ImGui 上下文，窗口过程里的 ImGui 调用只作用于“当前上下文”，
        // 所以分发前必须把接收消息的那个窗口的渲染器设为当前。
        if (settingsWindowReady_ && message.hwnd == settingsWindow_.Hwnd()) {
            settingsRenderer_.MakeCurrent();
        } else {
            renderer_.MakeCurrent();
        }
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }

    LeaveSelection();
    // 退出前把本次会话改动过的窗口全部恢复原状，否则用户关掉 Wind 后
    // 那些被调过透明度/置顶的窗口就永远停在半透明或置顶状态了。
    RestoreAllWindows();
    tray_.Destroy();
    settingsWindow_.Destroy();
    settingsView_.SetIcons(nullptr);
    settingsIcons_.Release();
    settingsRenderer_.Shutdown();
    toggleHotkey_.Unregister();
    toolbar_.SetIcons(nullptr);
    icons_.Release();
    renderer_.Shutdown();
    overlay_.Destroy();
    singleInstance_.Release();
    return static_cast<int>(message.wParam);
}

HotkeyStatus Application::LoadSettingsAndHotkey() {
    settings_ = core::SettingsStore::Load();
    if (!core::IsValidHotkey(settings_.toggleHotkey)) {
        platform::Log("设置里的快捷键非法，改用默认值 Ctrl+Alt+W。");
        settings_.toggleHotkey = core::kDefaultSettings.toggleHotkey;
    }

    activeHotkey_ = settings_.toggleHotkey;
    if (RegisterToggleHotkey(activeHotkey_)) {
        toggleHotkeyAvailable_ = true;
        platform::Log("全局热键已注册：%s", core::FormatHotkey(activeHotkey_).c_str());
        return HotkeyStatus::Ok;
    }

    // 不再尝试备选键：在用户机器上悄悄占用别的组合键，比“暂时没有全局热键”更糟。
    toggleHotkeyAvailable_ = false;
    platform::Log("配置的快捷键已被其他程序占用，本次启动没有全局热键，仅保留托盘入口。");
    return HotkeyStatus::Unavailable;
}

bool Application::RegisterToggleHotkey(const core::HotkeyBinding& binding) {
    if (!core::IsValidHotkey(binding)) {
        return false;
    }
    return toggleHotkey_.Register(nullptr, kToggleHotkeyId, binding.modifiers | MOD_NOREPEAT, binding.virtualKey);
}

bool Application::ApplyToggleHotkey(const core::HotkeyBinding& binding) {
    // 没改动就别重新注册：Register 会先注销旧键，成功的注册也会有一次空窗。
    if (toggleHotkeyAvailable_ && binding == activeHotkey_) {
        return true;
    }

    const core::HotkeyBinding previous = activeHotkey_;
    const bool hadPrevious = toggleHotkeyAvailable_;
    if (RegisterToggleHotkey(binding)) {
        activeHotkey_ = binding;
        toggleHotkeyAvailable_ = true;
        return true;
    }

    // 改坏了不能连旧键也丢掉，否则用户得重启才能恢复入口。
    if (hadPrevious) {
        RegisterToggleHotkey(previous);
        activeHotkey_ = previous;
        toggleHotkeyAvailable_ = true;
    } else {
        toggleHotkeyAvailable_ = false;
    }
    return false;
}

void Application::SyncTrayHotkey() {
    if (toggleHotkeyAvailable_) {
        tray_.SetToggleHotkey(Widen(core::FormatHotkey(activeHotkey_)), true);
    } else {
        tray_.SetToggleHotkey(std::wstring(), false);
    }
}

void Application::SyncAutoStart() {
    // 配置里的开关只是缓存，注册表才是真值：用户完全可能从任务管理器把它关掉。
    std::wstring stored;
    const platform::AutoStartState state = platform::QueryAutoStart(platform::kAutoStartValueName, &stored);

    if (!settings_.autoStart) {
        // 用户关着自启，注册表里却有项：那是他刻意打开的外部状态，不去动它。
        return;
    }

    switch (state) {
        case platform::AutoStartState::On:
            return;
        case platform::AutoStartState::OnStale:
            if (!platform::SetAutoStart(platform::kAutoStartValueName)) {
                platform::Log("自启项自愈失败，本次继续以手动方式运行。");
            }
            return;
        case platform::AutoStartState::Off:
            // 项被外部删掉或被任务管理器禁用：尊重用户的选择，把配置改回来而不是重写注册表，
            // 否则用户会看到自启项被本程序悄悄复活。
            settings_.autoStart = false;
            core::SettingsStore::Save(settings_);
            platform::Log("注册表里的自启项已不存在，配置同步为关闭。");
            return;
    }
}

void Application::HandleHotkey(int hotkeyId) {
    switch (hotkeyId) {
        case kToggleHotkeyId:
            // 设置页正开着时不去抢焦点，只把它提到前面。
            if (settingsOpen_) {
                ::SetForegroundWindow(settingsWindow_.Hwnd());
                return;
            }
            if (selecting_) {
                LeaveSelection();
            } else {
                EnterSelection();
            }
            break;

        case kEscapeHotkeyId:
            LeaveSelection();
            break;

        default:
            break;
    }
}

void Application::OnTrayCommand(ui::TrayIcon::Command command) {
    switch (command) {
        case ui::TrayIcon::Command::ToggleSelection:
            HandleHotkey(kToggleHotkeyId);
            break;
        case ui::TrayIcon::Command::Settings:
            OpenSettings();
            break;
        case ui::TrayIcon::Command::Quit:
            ::PostQuitMessage(0);
            break;
        case ui::TrayIcon::Command::None:
            break;
    }
}

void Application::EnterSelection() {
    if (selecting_) {
        return;
    }

    const RECT screen = platform::VirtualScreenBounds();
    selection_ = std::make_unique<core::Selection>(overlay_.Hwnd(), registry_);

    // 进入前先按最新显示器布局校正叠加层与交换链，避免换过分辨率后描边错位。
    overlay_.Show();
    renderer_.Resize(screen.right - screen.left, screen.bottom - screen.top);
    SyncUiScale();

    POINT cursor{};
    ::GetCursorPos(&cursor);
    selection_->UpdatePointer(cursor);

    escapeHotkey_.Register(nullptr, kEscapeHotkeyId, MOD_NOREPEAT, VK_ESCAPE);
    wheelHook_.Install(overlay_.Hwnd(), ui::OverlayWindow::kWheelMessage);

    selecting_ = true;
    tray_.SetSelecting(true);
    ::SetTimer(overlay_.Hwnd(), kFrameTimerId, kFrameIntervalMs, nullptr);
}

void Application::LeaveSelection() {
    if (!selecting_) {
        return;
    }

    selecting_ = false;
    tray_.SetSelecting(false);
    ::KillTimer(overlay_.Hwnd(), kFrameTimerId);
    wheelHook_.Uninstall();
    escapeHotkey_.Unregister();
    overlay_.Hide();
    // 已经施加到目标窗口的属性不会被还原——这是工具的本意；只有“重置”按钮才会还原。
    selection_.reset();
}

void Application::RenderFrame() {
    if (!selecting_ || !selection_) {
        return;
    }

    renderer_.BeginFrame();
    const RECT screen = platform::VirtualScreenBounds();
    const POINT origin{screen.left, screen.top};
    const ui::ToolbarView::Command command = toolbar_.Draw(*selection_, origin);
    renderer_.EndFrame();

    ApplyCommand(command);
}

void Application::ApplyCommand(const ui::ToolbarView::Command& command) {
    if (!selection_) {
        return;
    }

    switch (command.type) {
        case ui::ToolbarView::Command::Type::AdjustOpacity:
            selection_->AdjustOpacity(command.value);
            break;
        case ui::ToolbarView::Command::Type::SetOpacity:
            selection_->SetOpacity(command.value);
            break;
        case ui::ToolbarView::Command::Type::ToggleTopmost:
            selection_->ToggleAlwaysOnTop();
            break;
        case ui::ToolbarView::Command::Type::Reset:
            selection_->Reset();
            break;
        case ui::ToolbarView::Command::Type::None:
            break;
    }
}

void Application::OnPointerMoved(POINT screenPoint) {
    if (selection_ == nullptr) {
        return;
    }
    // 指针停在工具栏上时不再重新做命中测试：工具栏浮在目标窗口外侧，
    // 按指针重新选取会把目标换成工具栏底下的窗口，工具栏随之跳走或消失。
    // 工具栏与目标窗口之间的走廊同样要冻结，否则鼠标快速移向工具栏时仍会掉目标。
    if (toolbar_.ProtectsPoint(screenPoint)) {
        return;
    }
    selection_->UpdatePointer(screenPoint);
}

void Application::OnWheel(int delta) {
    if (selection_ == nullptr || delta == 0) {
        return;
    }
    selection_->AdjustOpacity(delta > 0 ? kOpacityWheelStep : -kOpacityWheelStep);
}

void Application::OnPrimaryClick() {
    // 工具栏上的点击由 ImGui 自行消费；叠加层空白区域的点击无需额外处理。
}

void Application::OnDismissRequested() {
    // 在工具栏上点右键不应退出选取模式，否则拖滑块时手滑就退出了。
    POINT cursor{};
    if (::GetCursorPos(&cursor) != FALSE && toolbar_.ContainsScreenPoint(cursor)) {
        return;
    }
    LeaveSelection();
}

void Application::OnDisplayChanged() {
    const RECT screen = platform::VirtualScreenBounds();
    renderer_.Resize(screen.right - screen.left, screen.bottom - screen.top);
    SyncUiScale();
}

void Application::SyncUiScale() {
    const float scale = platform::WindowDpiScale(overlay_.Hwnd());
    renderer_.SetUiScale(scale);
    toolbar_.SetUiScale(scale);
}

void Application::PreviewTheme(const core::Settings& settings) {
    const bool light = settings.UsesLightTheme(platform::SystemUsesLightTheme());
    lightTheme_ = light;
    theme_ = ui::MakeTheme(light, settings.accentRgb);
    renderer_.SetTheme(light, settings.accentRgb);
    toolbar_.SetTheme(theme_);
    settingsView_.SetTheme(theme_);
    if (settingsRendererReady_) {
        settingsRenderer_.SetTheme(light, settings.accentRgb);
    }
}

void Application::SyncTheme() {
    PreviewTheme(settings_);
    platform::Log("界面配色：%s", lightTheme_ ? "浅色" : "深色");
}

void Application::OnSystemThemeChanged() {
    // 强制亮色/暗色时系统主题变化与本程序无关，这里只按解析结果判断是否需要重刷。
    if (settings_.UsesLightTheme(platform::SystemUsesLightTheme()) == lightTheme_) {
        return;
    }
    SyncTheme();
}

void Application::OnSettingsCloseRequested() {
    CloseSettings(false);
}

bool Application::OnSettingsKeyDown(UINT virtualKey, UINT modifiers) {
    if (!settingsOpen_) {
        return false;
    }
    return settingsView_.OnKeyDown(virtualKey, modifiers);
}

void Application::OpenSettings() {
    if (!settingsWindowReady_) {
        return;
    }
    if (settingsOpen_) {
        ::SetForegroundWindow(settingsWindow_.Hwnd());
        return;
    }

    // 设置页与选取态互斥：两边都要 ImGui 输入、都要键盘，叠在一起必然互相抢。
    LeaveSelection();

    // 打开前先把自启项的真实状态同步进配置：用户可能在关掉本程序期间从任务管理器
    // 禁用或删除了自启项，页面必须显示“现在到底会不会自启”，而不是上次保存的旧值。
    const bool autoStartActuallyOn =
        platform::QueryAutoStart(platform::kAutoStartValueName) != platform::AutoStartState::Off;
    if (settings_.autoStart != autoStartActuallyOn) {
        settings_.autoStart = autoStartActuallyOn;
        core::SettingsStore::Save(settings_);
    }

    // 第二个参数故意传当前生效的热键，页面据此区分“配置值”和“真正能用的值”。
    core::Settings activeSnapshot = settings_;
    activeSnapshot.toggleHotkey = activeHotkey_;
    settingsView_.Open(settings_, activeSnapshot, toggleHotkeyAvailable_);
    if (!toggleHotkeyAvailable_) {
        settingsView_.SetNotice("当前没有生效的全局快捷键，请在这里重新设置。");
    }

    settingsWindow_.Show(kSettingsWidth, kSettingsHeight);
    settingsOpen_ = true;

    RECT client{};
    ::GetClientRect(settingsWindow_.Hwnd(), &client);
    const int width = static_cast<int>(client.right - client.left);
    const int height = static_cast<int>(client.bottom - client.top);
    if (!settingsRendererReady_) {
        // 懒初始化：D3D 设备 + 交换链只在真正要显示设置页时创建。
        if (!settingsRenderer_.Initialize(settingsWindow_.Hwnd(), width, height)) {
            settingsOpen_ = false;
            settingsWindow_.Hide();
            ::MessageBoxW(nullptr, L"设置页的渲染器初始化失败，详细信息见 %TEMP%\\Wind.log。", L"Wind",
                          MB_ICONERROR | MB_OK);
            return;
        }
        settingsRendererReady_ = true;
        // 图标纹理跟着设备走，设置页的设备与工具栏不同，必须在这里单独解码上传一份。
        settingsIcons_.Load(settingsRenderer_.Device());
        settingsView_.SetIcons(&settingsIcons_);
    } else {
        settingsRenderer_.Resize(width, height);
    }

    settingsRenderer_.SetUiScale(settingsWindow_.DpiScale());
    settingsView_.SetUiScale(settingsWindow_.DpiScale());
    PreviewTheme(settingsView_.Working());
    ::SetTimer(settingsWindow_.Hwnd(), kSettingsTimerId, kFrameIntervalMs, nullptr);
}

void Application::CloseSettings(bool confirmed) {
    if (!settingsOpen_) {
        return;
    }

    settingsOpen_ = false;
    ::KillTimer(settingsWindow_.Hwnd(), kSettingsTimerId);
    settingsWindow_.Hide();
    settingsView_.SetNotice("");
    settingsView_.SetError("");
    // 无论确定还是取消，都按落盘后的设置重算一次：取消即回滚掉预览。
    SyncTheme();
    platform::Log("设置页已关闭（%s）。", confirmed ? "确定" : "取消");
}

void Application::RenderSettingsFrame() {
    if (!settingsOpen_ || !settingsRendererReady_) {
        return;
    }

    // 页面随时可能因为 DPI 变化改变客户区尺寸，重建交换链的代价远小于画错。
    RECT client{};
    ::GetClientRect(settingsWindow_.Hwnd(), &client);
    settingsRenderer_.Resize(static_cast<int>(client.right - client.left),
                             static_cast<int>(client.bottom - client.top));

    const float scale = settingsWindow_.DpiScale();
    settingsRenderer_.SetUiScale(scale);
    settingsView_.SetUiScale(scale);
    // 实时预览：每帧都按工作副本重算配色，确定之前的任何改动都能立刻看到效果。
    PreviewTheme(settingsView_.Working());

    settingsRenderer_.BeginFrame();
    const ui::SettingsView::Action action = settingsView_.Draw();
    settingsRenderer_.EndFrame();

    if (action == ui::SettingsView::Action::Cancel) {
        CloseSettings(false);
        return;
    }
    if (action != ui::SettingsView::Action::Confirm) {
        return;
    }

    const core::Settings working = settingsView_.Working();

    // 自启项先写：热键一旦换成功就不好回退，把可能失败的步骤全排在它前面，
    // 失败时页面保持原样、提示后返回，用户改完再点确定。
    if (working.autoStart != settings_.autoStart) {
        const bool applied = working.autoStart ? platform::SetAutoStart(platform::kAutoStartValueName)
                                               : platform::RemoveAutoStart(platform::kAutoStartValueName);
        if (!applied) {
            settingsView_.SetError("无法修改开机自启项，请检查是否有安全软件拦截。");
            return;
        }
    }

    if (!ApplyToggleHotkey(working.toggleHotkey)) {
        // 冲突只提示、不关窗：用户改一个能用的组合键再点确定即可。
        settingsView_.SetError("该快捷键已被其他程序占用，请换一个组合键。");
        return;
    }

    settings_ = working;
    core::SettingsStore::Save(settings_);
    SyncTrayHotkey();
    CloseSettings(true);
}

void Application::RestoreAllWindows() {
    // 已经离开选取模式时 selection_ 为空，但快照仍在 registry_ 里，所以退出还原不受影响。
    const size_t total = registry_.Size();
    const size_t restored = registry_.RestoreAll();
    if (total > 0) {
        platform::Log("退出还原完成：记录 %zu 个窗口，实际还原 %zu 个。", total, restored);
    }
}

}  // namespace wind::app
