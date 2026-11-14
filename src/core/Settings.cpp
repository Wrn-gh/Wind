#include "wind/core/Settings.h"

namespace wind::core {
namespace {

/// 允许出现在组合键里的修饰键；其余位（例如 MOD_NOREPEAT）不参与“是否按了修饰键”的判断。
constexpr UINT kModifierMask = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

}  // namespace

bool IsValidHotkey(const HotkeyBinding& binding) {
    return binding.virtualKey != 0 && (binding.modifiers & kModifierMask) != 0;
}

std::string VirtualKeyName(UINT virtualKey) {
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        return std::string(1, static_cast<char>(virtualKey));
    }
    if (virtualKey >= '0' && virtualKey <= '9') {
        return std::string(1, static_cast<char>(virtualKey));
    }
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        return "F" + std::to_string(virtualKey - VK_F1 + 1);
    }

    switch (virtualKey) {
        case VK_LEFT:
            return "Left";
        case VK_RIGHT:
            return "Right";
        case VK_UP:
            return "Up";
        case VK_DOWN:
            return "Down";
        case VK_HOME:
            return "Home";
        case VK_END:
            return "End";
        case VK_PRIOR:
            return "PageUp";
        case VK_NEXT:
            return "PageDown";
        case VK_INSERT:
            return "Insert";
        case VK_DELETE:
            return "Delete";
        case VK_SPACE:
            return "Space";
        case VK_TAB:
            return "Tab";
        case VK_BACK:
            return "Backspace";
        case VK_RETURN:
            return "Enter";
        case VK_ESCAPE:
            return "Esc";
        case VK_OEM_1:
            return ";";
        case VK_OEM_2:
            return "/";
        case VK_OEM_3:
            return "`";
        case VK_OEM_4:
            return "[";
        case VK_OEM_5:
            return "\\";
        case VK_OEM_6:
            return "]";
        case VK_OEM_7:
            return "'";
        case VK_OEM_PLUS:
            return "=";
        case VK_OEM_MINUS:
            return "-";
        case VK_OEM_COMMA:
            return ",";
        case VK_OEM_PERIOD:
            return ".";
        default:
            break;
    }
    return "?";
}

std::string FormatHotkey(const HotkeyBinding& binding) {
    std::string text;
    if ((binding.modifiers & MOD_CONTROL) != 0) {
        text += "Ctrl+";
    }
    if ((binding.modifiers & MOD_ALT) != 0) {
        text += "Alt+";
    }
    if ((binding.modifiers & MOD_SHIFT) != 0) {
        text += "Shift+";
    }
    if ((binding.modifiers & MOD_WIN) != 0) {
        text += "Win+";
    }
    text += VirtualKeyName(binding.virtualKey);
    return text;
}

}  // namespace wind::core
