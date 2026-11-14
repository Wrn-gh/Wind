#include "wind/ui/TrayIcon.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <utility>

#include "resource.h"
#include "wind/platform/SingleInstance.h"
#include "wind/platform/Win32Error.h"

namespace wind::ui {
namespace {

constexpr const wchar_t* kWindowClass = L"WindTrayHostWindow";
/// 外部图标文件名：放在 exe 同目录可临时替换图标，没有则用编进 exe 的 IDI_WIND_ICON。
constexpr const wchar_t* kIconFileName = L"wind.ico";

/// @brief 图标边长：跟随系统小图标尺寸，但至少 32 像素，缩小后仍能保持形状。
int IconSize() {
    const int width = ::GetSystemMetrics(SM_CXSMICON);
    const int height = ::GetSystemMetrics(SM_CYSMICON);
    return std::max(32, std::max(width, height));
}

/// @brief 读取 exe 同目录下的 wind.ico，不存在时返回 nullptr（不会弹任何提示）。
HICON LoadExternalIcon(int size) {
    wchar_t path[MAX_PATH]{};
    const DWORD length = ::GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path)) {
        return nullptr;
    }
    wchar_t* separator = ::wcsrchr(path, L'\\');
    if (separator == nullptr) {
        return nullptr;
    }
    separator[1] = L'\0';
    if (::wcscat_s(path, kIconFileName) != 0) {
        return nullptr;
    }
    if (::GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        return nullptr;
    }
    return static_cast<HICON>(::LoadImageW(nullptr, path, IMAGE_ICON, size, size, LR_LOADFROMFILE));
}

/// @brief 运行时绘制占位图标：蓝色圆角方块 + 向右渐隐，暗示“透明度”。
HICON RenderPlaceholderIcon(int size) {
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    // 负高度表示自上而下存行，省掉后面翻转坐标。
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    HDC screen = ::GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP color = ::CreateDIBSection(screen, reinterpret_cast<const BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits,
                                       nullptr, 0);
    ::ReleaseDC(nullptr, screen);
    if (color == nullptr || bits == nullptr) {
        if (color != nullptr) {
            ::DeleteObject(color);
        }
        return nullptr;
    }

    auto* pixels = static_cast<DWORD*>(bits);
    const float span = static_cast<float>(size);
    const float center = (span - 1.0f) * 0.5f;
    const float radius = std::max(1.0f, center * 0.42f);
    const float straight = center - radius;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = std::fabs(static_cast<float>(x) - center) - straight;
            const float dy = std::fabs(static_cast<float>(y) - center) - straight;
            const float overX = std::max(dx, 0.0f);
            const float overY = std::max(dy, 0.0f);
            const float distance = std::sqrt(overX * overX + overY * overY);
            const float coverage = (dx <= 0.0f && dy <= 0.0f) ? 1.0f : std::clamp(radius + 0.5f - distance, 0.0f, 1.0f);
            if (coverage <= 0.0f) {
                pixels[y * size + x] = 0;
                continue;
            }

            // 右侧渐隐 + 顶部略亮，让 16px 的小图也有层次。
            const float fade = 1.0f - 0.55f * (static_cast<float>(x) / std::max(1.0f, span - 1.0f));
            const float shade = 0.78f + 0.22f * (1.0f - static_cast<float>(y) / std::max(1.0f, span - 1.0f));
            const auto toByte = [](float value) {
                return static_cast<DWORD>(std::lround(std::clamp(value, 0.0f, 255.0f)));
            };
            const DWORD alpha = toByte(coverage * fade * 255.0f);
            const DWORD red = toByte(6.0f * shade);
            const DWORD green = toByte(168.0f * shade);
            const DWORD blue = toByte(255.0f * shade);
            pixels[y * size + x] = (alpha << 24) | (red << 16) | (green << 8) | blue;
        }
    }

    // 掩码全 0：透明度完全交给 32 位色的 alpha 通道。
    HBITMAP mask = ::CreateBitmap(size, size, 1, 1, nullptr);
    HDC maskDc = ::CreateCompatibleDC(nullptr);
    if (mask != nullptr && maskDc != nullptr) {
        HGDIOBJ previous = ::SelectObject(maskDc, mask);
        ::PatBlt(maskDc, 0, 0, size, size, BLACKNESS);
        ::SelectObject(maskDc, previous);
    }

    HICON icon = nullptr;
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = color;
    if (mask != nullptr) {
        icon = ::CreateIconIndirect(&info);
    }

    if (maskDc != nullptr) {
        ::DeleteDC(maskDc);
    }
    if (mask != nullptr) {
        ::DeleteObject(mask);
    }
    ::DeleteObject(color);
    return icon;
}

/// @brief 读取编进 exe 的 wind.ico（IDI_WIND_ICON），保证单文件分发时也有图标。
HICON LoadEmbeddedIcon(int size) {
    return static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_WIND_ICON),
                                           IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
}

/// @brief 优先用外部 wind.ico（调试时可随手替换），其次用内嵌图标，最后退回占位图标。
HICON CreateAppIcon(int size) {
    if (HICON external = LoadExternalIcon(size); external != nullptr) {
        return external;
    }
    if (HICON embedded = LoadEmbeddedIcon(size); embedded != nullptr) {
        return embedded;
    }
    return RenderPlaceholderIcon(size);
}

/// @brief 悬浮提示：Wind 是窗口工具，热键只是入口之一，没有热键时也要能自解释。
std::wstring BuildToolTip(const std::wstring& hotkey, bool available) {
    if (hotkey.empty() || !available) {
        return L"Wind — 窗口工具（点击打开菜单）";
    }
    return L"Wind — 窗口工具（" + hotkey + L" 开始选取）";
}

}  // namespace

TrayIcon::~TrayIcon() { Destroy(); }

bool TrayIcon::Create(HINSTANCE instance, Listener* listener) {
    if (hwnd_ != nullptr) {
        return true;
    }

    listener_ = listener;
    taskbarCreatedMessage_ = ::RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &TrayIcon::WndProc;
    description.hInstance = instance;
    description.lpszClassName = kWindowClass;
    if (::RegisterClassExW(&description) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        platform::Log("托盘窗口类注册失败：%s", platform::LastErrorMessage(::GetLastError()).c_str());
        return false;
    }

    // 只作为消息宿主，窗口本身不需要显示。
    hwnd_ = ::CreateWindowExW(0, kWindowClass, L"Wind", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        platform::Log("托盘宿主窗口创建失败：%s", platform::LastErrorMessage(::GetLastError()).c_str());
        return false;
    }

    icon_ = CreateAppIcon(IconSize());
    AddIcon();
    if (!added_) {
        platform::Log("托盘图标注册失败：%s", platform::LastErrorMessage(::GetLastError()).c_str());
        return false;
    }
    platform::Log("托盘图标已注册。");
    return true;
}

void TrayIcon::Destroy() {
    RemoveIcon();
    if (icon_ != nullptr) {
        ::DestroyIcon(icon_);
        icon_ = nullptr;
    }
    if (hwnd_ != nullptr) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void TrayIcon::AddIcon() {
    if (hwnd_ == nullptr || added_) {
        return;
    }

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kCallbackMessage;
    data.hIcon = icon_;
    const std::wstring tip = BuildToolTip(toggleHotkeyText_, hotkeyAvailable_);
    ::lstrcpynW(data.szTip, tip.c_str(), _countof(data.szTip));
    added_ = ::Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (!added_) {
        return;
    }

    // 用版本 4 的回调语义：左键单击以 NIN_SELECT 通知，键盘选中也能响应。
    data.uVersion = NOTIFYICON_VERSION_4;
    ::Shell_NotifyIconW(NIM_SETVERSION, &data);
}

void TrayIcon::RemoveIcon() {
    if (hwnd_ == nullptr || !added_) {
        return;
    }

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kIconId;
    ::Shell_NotifyIconW(NIM_DELETE, &data);
    added_ = false;
}

void TrayIcon::ShowMenu(POINT anchor) {
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    std::wstring toggleLabel = selecting_ ? L"结束选取" : L"开始选取";
    if (hotkeyAvailable_ && !toggleHotkeyText_.empty()) {
        toggleLabel += L"\t" + toggleHotkeyText_;
    }
    ::AppendMenuW(menu, MF_STRING, kMenuToggle, toggleLabel.c_str());
    ::SetMenuDefaultItem(menu, kMenuToggle, FALSE);
    // 退出只走托盘菜单：可配置热键只有「开始选取」一个，避免和别的程序抢热键。
    ::AppendMenuW(menu, MF_STRING, kMenuSettings, L"设置…");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kMenuQuit, L"退出");

    // 菜单必须由当前前台窗口弹出，否则点菜单外面不会关闭；宿主窗口是隐藏的，
    // 这里 SetForegroundWindow 不一定成功，所以再用 PostMessage(WM_NULL) 兜底。
    ::SetForegroundWindow(hwnd_);
    const UINT selected = ::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, anchor.x, anchor.y,
                                             hwnd_, nullptr);
    ::PostMessageW(hwnd_, WM_NULL, 0, 0);
    ::DestroyMenu(menu);

    if (listener_ == nullptr) {
        return;
    }
    if (selected == kMenuToggle) {
        listener_->OnTrayCommand(Command::ToggleSelection);
    } else if (selected == kMenuSettings) {
        listener_->OnTrayCommand(Command::Settings);
    } else if (selected == kMenuQuit) {
        listener_->OnTrayCommand(Command::Quit);
    }
}

void TrayIcon::SetToggleHotkey(std::wstring text, bool available) {
    toggleHotkeyText_ = std::move(text);
    hotkeyAvailable_ = available;
    UpdateTip();
}

void TrayIcon::UpdateTip() {
    if (hwnd_ == nullptr || !added_) {
        return;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kIconId;
    data.uFlags = NIF_TIP | NIF_SHOWTIP;
    const std::wstring tip = BuildToolTip(toggleHotkeyText_, hotkeyAvailable_);
    ::lstrcpynW(data.szTip, tip.c_str(), _countof(data.szTip));
    ::Shell_NotifyIconW(NIM_MODIFY, &data);
}

void TrayIcon::Notify(const wchar_t* title, const wchar_t* text, bool warning) {
    if (hwnd_ == nullptr || !added_) {
        return;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kIconId;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = warning ? NIIF_WARNING : NIIF_INFO;
    ::lstrcpynW(data.szInfoTitle, (title != nullptr) ? title : L"", _countof(data.szInfoTitle));
    ::lstrcpynW(data.szInfo, (text != nullptr) ? text : L"", _countof(data.szInfo));
    data.uTimeout = 10000;
    ::Shell_NotifyIconW(NIM_MODIFY, &data);
}

LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    TrayIcon* self = reinterpret_cast<TrayIcon*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<TrayIcon*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    if (self == nullptr) {
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->HandleMessage(hwnd, message, wParam, lParam);
}

LRESULT TrayIcon::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // 资源管理器重启会清空通知区，收到广播后必须重新注册图标。
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        added_ = false;
        AddIcon();
        return 0;
    }

    // 重复启动 exe 时，新进程会发这条注册消息过来：把设置页提出来，
    // 用户才知道程序已经在跑，不用去翻托盘。
    if (message == platform::SingleInstance::RequestMessage()) {
        if (listener_ != nullptr) {
            listener_->OnTrayCommand(Command::Settings);
        }
        return 0;
    }

    // 安装包升级、卸载，或脚本执行 `Wind.exe --quit` 时走这条：和点托盘菜单的「退出」
    // 完全一样，会先还原改过的窗口再结束进程。
    if (message == platform::SingleInstance::QuitMessage()) {
        if (listener_ != nullptr) {
            listener_->OnTrayCommand(Command::Quit);
        }
        return 0;
    }

    if (message == kCallbackMessage) {
        // 版本 4 起 lParam 直接是事件消息，鼠标位置在 wParam 的高低字里（托盘图标锚点）。
        // 同时保留旧语义分支，避免 NIM_SETVERSION 失败时收不到任何点击。
        const UINT event = LOWORD(lParam);
        // 点击气泡直接打开设置页：这正是热键降级提示的落点。
        if (event == NIN_BALLOONUSERCLICK) {
            if (listener_ != nullptr) {
                listener_->OnTrayCommand(Command::Settings);
            }
            return 0;
        }
        if (event != NIN_SELECT && event != NIN_KEYSELECT && event != WM_LBUTTONUP && event != WM_RBUTTONUP &&
            event != WM_CONTEXTMENU) {
            return 0;
        }

        POINT anchor{};
        const bool hasAnchor = event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_CONTEXTMENU;
        if (hasAnchor) {
            anchor.x = static_cast<short>(LOWORD(wParam));
            anchor.y = static_cast<short>(HIWORD(wParam));
        }
        if (anchor.x == 0 && anchor.y == 0) {
            ::GetCursorPos(&anchor);
        }
        ShowMenu(anchor);
        return 0;
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace wind::ui
