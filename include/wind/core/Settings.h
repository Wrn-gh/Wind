#pragma once

#include <windows.h>

#include <string>

namespace wind::core {

/// @brief 一组全局热键：修饰键（MOD_*）+ 虚拟键码。
///
/// 修饰键里不含 MOD_NOREPEAT —— 那是注册时的行为开关，由调用方在注册前补上，
/// 这样配置文件的数值与用户看到的组合键是同一个东西。
struct HotkeyBinding {
    UINT modifiers = MOD_CONTROL | MOD_ALT;
    UINT virtualKey = 'W';

    bool operator==(const HotkeyBinding&) const = default;
};

/// @brief 主题三态：跟随系统 / 强制亮色 / 强制暗色。
enum class ThemeMode { System, Light, Dark };

/// @brief 用户可配置的全部设置项（纯数据，不涉及 io）。
struct Settings {
    /// @brief 开始 / 结束选取的全局热键。
    HotkeyBinding toggleHotkey{};
    /// @brief 界面配色来源。
    ThemeMode themeMode = ThemeMode::System;
    /// @brief 强调色 0xRRGGBB；0 表示沿用主题自带强调色（升级后观感零变化）。
    uint32_t accentRgb = 0;
    /// @brief 「退出热键已取消」的提示是否已经展示过，避免每次启动都弹。
    bool quitHotkeyNoticeShown = false;
    /// @brief 是否开启开机自启。
    /// @note 它只是注册表状态的缓存，真值始终以 HKCU\...\Run 为准：
    ///       用户可能从任务管理器关掉自启，那时这里会跟着被纠正。
    bool autoStart = false;

    bool operator==(const Settings&) const = default;

    /// @brief 结合系统当前主题解析出最终该用亮色还是暗色。
    bool UsesLightTheme(bool systemLight) const {
        switch (themeMode) {
            case ThemeMode::Light:
                return true;
            case ThemeMode::Dark:
                return false;
            case ThemeMode::System:
                break;
        }
        return systemLight;
    }
};

inline constexpr Settings kDefaultSettings{};

/// @brief 配置是否合法：至少要按一个修饰键，且虚拟键码非 0。
///
/// 只按字母不按 Ctrl/Alt/Shift/Win 的组合无法注册成全局热键，
/// 也会把正常打字全部吞掉，因此读取配置时直接判定为非法并回落默认值。
bool IsValidHotkey(const HotkeyBinding& binding);

/// @brief 虚拟键码的可读名字（A-Z、0-9、F1-F24、方向键等），未知时返回 "?"。
std::string VirtualKeyName(UINT virtualKey);

/// @brief 格式化成用户看到的组合键文本，例如 "Ctrl+Alt+W"。
std::string FormatHotkey(const HotkeyBinding& binding);

}  // namespace wind::core
