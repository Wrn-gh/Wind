#pragma once

#include <string>

#include "wind/core/Settings.h"

namespace wind::ui {

/// @brief 界面文案的键。
///
/// 界面只提供简体中文与英文两套；`core::Language::System` 在进入这里之前就应该
/// 被解析成具体语言（见 core::Settings::UsesChineseUi），传进来时按中文处理。
enum class Text : int {
    // 设置页
    SettingsTitle,
    SettingsTitleRecording,
    SectionGeneral,
    SectionIgnore,
    SectionAppearance,
    LabelPickHotkey,
    LabelIgnoreHotkey,
    ButtonDefault,
    ButtonRecording,
    HintHotkeyRecord,
    ErrorHotkeyNoModifier,
    ErrorHotkeySame,
    HintHotkeyUnavailableAny,
    HintPickHotkeyUnavailable,
    HintIgnoreHotkeyUnavailable,
    HintAppliesOnConfirm,
    LabelAutoStart,
    HintAutoStart,
    LabelLanguage,
    OptionSystem,
    OptionLight,
    OptionDark,
    CaptionIgnoreList,
    IgnoreListEmpty,
    ButtonRemove,
    HintIgnoreHowToAdd,
    LabelTheme,
    LabelAccent,
    ButtonResetAccent,
    ButtonConfirm,
    ButtonCancel,
    // 工具栏与提示条
    ToolbarPin,
    ToolbarPinned,
    ToolbarLocked,
    ToolbarReset,
    HintPickMode,
    HintIgnoreTarget,
    HintIgnoreNoTarget,
    // 托盘
    TrayTipIdle,
    TrayTipHotkey,
    TrayStopPicking,
    TrayStartPicking,
    TrayIgnorePicker,
    TraySettings,
    TrayQuit,
    // 应用层：错误、气泡通知、消息框
    ErrorNoOverlay,
    ErrorNoDevice,
    ErrorNoEntry,
    NotifyHotkeyBusyTitleAll,
    NotifyHotkeyBusyTitlePick,
    NotifyHotkeyBusyTitleIgnore,
    NotifyHotkeyBusyText,
    NotifyQuitHotkeyTitle,
    NotifyQuitHotkeyText,
    NotifyIgnoreExistsTitle,
    NotifyIgnoreExistsText,
    NotifyIgnoreNoProcessTitle,
    NotifyIgnoreNoProcessText,
    NotifyIgnoreFullTitle,
    NotifyIgnoreFullText,
    NotifyIgnoreBadRuleTitle,
    NotifyIgnoreBadRuleText,
    NotifyIgnoreAddedText,
    NoticeHotkeyNotActive,
    ErrorAutoStartFailed,
    ErrorPickHotkeyBusy,
    ErrorIgnoreHotkeyBusy,
    ErrorSettingsRenderer,
    WindowSettingsTitle,
    Count,
};

/// @brief 取界面文案（UTF-8），返回值始终非空。
const char* Tr(core::Language language, Text key);

/// @brief 取带参数的界面文案；表里的占位符用 printf 风格。
std::string TrFormat(core::Language language, Text key, ...);

/// @brief 宽字符版本：托盘菜单、气泡通知与 MessageBox 只吃 wchar_t。
std::wstring TrWide(core::Language language, Text key);
std::wstring TrFormatWide(core::Language language, Text key, ...);

}  // namespace wind::ui
