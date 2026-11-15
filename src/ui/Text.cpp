#include "wind/ui/Text.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <vector>

#include "wind/platform/Text.h"

namespace wind::ui {
namespace {

/// @brief 一条界面文案的中英对照。
///
/// 两套文案写在同一行，改文案时很难只改一半；枚举与表数量由下面的 static_assert
/// 在编译期对齐，加一条文案必须同时改两处，漏了会直接编译失败。
struct Entry {
    const char* zh;
    const char* en;
};

constexpr Entry kEntries[] = {
    // 设置页
    {"设置", "Settings"},
    {"设置 · 录制快捷键", "Settings · Recording"},
    {"常规", "General"},
    {"忽略窗口", "Ignored windows"},
    {"外观", "Appearance"},
    {"选取快捷键", "Pick hotkey"},
    {"忽略快捷键", "Ignore hotkey"},
    {"默认", "Default"},
    {"按下组合键…", "Press keys…"},
    {"点击按钮后按下新的组合键，Esc 取消录制", "Click a button, then press a new combo. Esc cancels."},
    {"快捷键至少要包含一个 Ctrl / Alt / Shift / Win 修饰键。",
     "A hotkey needs at least one Ctrl / Alt / Shift / Win key."},
    {"两个快捷键不能相同，请换一个组合键。", "The two hotkeys must differ, pick another combo."},
    {"当前有快捷键不可用，请重新设置。", "Some hotkeys are unavailable, set them again."},
    {"选取快捷键当前不可用，请重新设置", "The pick hotkey is unavailable, set it again"},
    {"忽略快捷键当前不可用，请重新设置", "The ignore hotkey is unavailable, set it again"},
    {"确定后生效", "Applied on OK"},
    {"开机自启", "Launch at login"},
    {"登录 Windows 后自动启动", "Start after you sign in"},
    {"语言", "Language"},
    {"跟随系统", "Follow system"},
    {"亮色", "Light"},
    {"暗色", "Dark"},
    {"选取时跳过这些窗口", "Skipped while picking"},
    {"还没有忽略任何窗口", "No ignored windows yet"},
    {"移除", "Remove"},
    {"按忽略快捷键在目标窗口上点击即可加入", "Press the ignore hotkey, then click a window to add it"},
    {"主题", "Theme"},
    {"主题色", "Accent"},
    {"恢复默认", "Reset"},
    {"确定", "OK"},
    {"取消", "Cancel"},
    // 工具栏与提示条
    {"置顶", "Pin"},
    {"已置顶", "Pinned"},
    {"已锁定", "Locked"},
    {"重置", "Reset"},
    {"移动鼠标选择目标窗口 · 右键或 Esc 退出", "Move the mouse to pick a window · right-click or Esc exits"},
    {"左键点击加入忽略列表 · 右键或 Esc 取消", "Left-click to ignore this window · right-click or Esc cancels"},
    {"把光标移到要忽略的窗口上 · 右键或 Esc 取消", "Move the cursor over a window · right-click or Esc cancels"},
    // 托盘
    {"Wind — 窗口工具（点击打开菜单）", "Wind — window tool (click for menu)"},
    {"Wind — 窗口工具（%s 开始选取）", "Wind — window tool (%s to start picking)"},
    {"结束选取", "Stop picking"},
    {"开始选取", "Start picking"},
    {"选取要忽略的窗口", "Pick window to ignore"},
    {"设置…", "Settings…"},
    {"退出", "Quit"},
    // 应用层
    {"无法创建叠加层窗口，详细信息见 %TEMP%\\Wind.log。", "Could not create the overlay window. See %TEMP%\\Wind.log."},
    {"Direct3D 设备初始化失败，详细信息见 %TEMP%\\Wind.log。",
     "Direct3D device creation failed. See %TEMP%\\Wind.log."},
    {"Wind 无法注册全局快捷键，托盘图标也创建失败，程序没有任何操作入口。\n请检查系统资源后重启，详细信息见 %TEMP%\\Wind.log。",
     "Wind could not register any hotkey and the tray icon failed as well, so the program has no way to be "
     "controlled.\nCheck system resources and restart. See %TEMP%\\Wind.log."},
    {"快捷键均不可用", "No hotkey available"},
    {"选取快捷键不可用", "Pick hotkey unavailable"},
    {"忽略快捷键不可用", "Ignore hotkey unavailable"},
    {"配置的快捷键已被其他程序占用，请通过托盘菜单的「设置…」换一个。点击此处修改。",
     "Another program took the configured hotkey. Choose a new one in the tray menu under Settings…. Click here "
     "to fix it."},
    {"退出快捷键已取消", "Quit hotkey removed"},
    {"为避免与其他程序抢热键，Ctrl+Alt+Q 不再保留；请通过托盘菜单退出。",
     "Ctrl+Alt+Q is no longer reserved so it will not fight other programs for hotkeys; quit from the tray menu "
     "instead."},
    {"这个窗口已在忽略列表中", "This window is already ignored"},
    {"无需重复添加。", "No need to add it twice."},
    {"无法识别这个窗口所属的进程", "Cannot identify this window's process"},
    {"该窗口属于权限更高的进程（例如以管理员身份运行的程序），Wind 读不到它的信息。",
     "It belongs to a higher-privilege process (for example a program running as administrator), so Wind cannot "
     "read its details."},
    {"忽略列表已满", "Ignore list is full"},
    {"最多只能保存 64 条规则，请先在设置里删除一些。", "At most 64 rules are kept, delete some in Settings first."},
    {"无法添加这条规则", "Cannot add this rule"},
    {"这条规则不合法或已经存在，可在设置里查看当前列表。",
     "The rule is invalid or already exists, see the current list in Settings."},
    {"以后按选取快捷键时，这个窗口会被跳过。", "This window will be skipped next time you pick."},
    {"当前有快捷键没有生效，请在这里重新设置。", "A hotkey is not active, set it again here."},
    {"无法修改开机自启项，请检查是否有安全软件拦截。",
     "Could not change the startup entry, check whether security software blocks it."},
    {"选取快捷键已被其他程序占用，请换一个组合键。", "Another program uses that pick hotkey, choose another combo."},
    {"忽略快捷键已被其他程序占用，请换一个组合键。", "Another program uses that ignore hotkey, choose another combo."},
    {"设置页的渲染器初始化失败，详细信息见 %TEMP%\\Wind.log。",
     "The settings renderer failed to initialize. See %TEMP%\\Wind.log."},
    {"Wind 设置", "Wind Settings"},
};

static_assert(std::size(kEntries) == static_cast<std::size_t>(Text::Count),
              "文案表条目数与 Text 枚举不一致，请同步修改 include/wind/ui/Text.h");

const Entry& Lookup(Text key) {
    std::size_t index = static_cast<std::size_t>(key);
    if (index >= std::size(kEntries)) {
        // 越界只可能来自后续改动漏同步；回落到第一条，界面上出现错文案也比崩掉好。
        index = 0;
    }
    return kEntries[index];
}

bool IsEnglish(core::Language language) {
    return language == core::Language::English;
}

std::string Format(const char* format, va_list args) {
    va_list probe;
    va_copy(probe, args);
    const int size = ::vsnprintf(nullptr, 0, format, probe);
    va_end(probe);
    if (size <= 0) {
        return format;
    }
    std::vector<char> buffer(static_cast<std::size_t>(size) + 1, '\0');
    ::vsnprintf(buffer.data(), buffer.size(), format, args);
    return std::string(buffer.data(), static_cast<std::size_t>(size));
}

}  // namespace

const char* Tr(core::Language language, Text key) {
    const Entry& entry = Lookup(key);
    return IsEnglish(language) ? entry.en : entry.zh;
}

std::string TrFormat(core::Language language, Text key, ...) {
    va_list args;
    va_start(args, key);
    std::string text = Format(Tr(language, key), args);
    va_end(args);
    return text;
}

std::wstring TrWide(core::Language language, Text key) {
    return platform::Widen(Tr(language, key));
}

std::wstring TrFormatWide(core::Language language, Text key, ...) {
    va_list args;
    va_start(args, key);
    std::string text = Format(Tr(language, key), args);
    va_end(args);
    return platform::Widen(text);
}

}  // namespace wind::ui
