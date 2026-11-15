#include "wind/ui/SettingsView.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <string>

#include "wind/core/Settings.h"
#include "wind/platform/Text.h"
#include "wind/ui/IconSet.h"
#include "wind/ui/Text.h"

namespace wind::ui {
namespace {

// 96 DPI 下的设计尺寸；实际像素由 uiScale_ 换算。
constexpr float kTitleBarHeight = 40.0f;
constexpr float kPaddingX = 20.0f;
constexpr float kPaddingY = 14.0f;
// 标签列宽度按英文排版定：Pick hotkey / Launch at login 在 84 下会被截。
constexpr float kLabelWidth = 104.0f;
constexpr float kRowHeight = 30.0f;
constexpr float kButtonWidth = 90.0f;
constexpr float kFooterHeight = 54.0f;
constexpr float kSectionGap = 18.0f;
constexpr float kLineGap = 5.0f;
constexpr float kHotkeyWidth = 180.0f;
constexpr float kResetWidth = 56.0f;
constexpr float kControlGap = 8.0f;

/// 忽略列表：固定高。+ 内部滚动，规则再多也不会把设置页撑长。
constexpr float kIgnoreListHeight = 104.0f;
constexpr float kIgnoreRowHeight = 26.0f;
constexpr float kIgnoreRemoveWidth = 48.0f;

/// @brief 主题分段按钮的顺序按界面习惯排列，不依赖 core::ThemeMode 的枚举值顺序。
constexpr core::ThemeMode kThemeOptions[] = {
    core::ThemeMode::Light,
    core::ThemeMode::Dark,
    core::ThemeMode::System,
};

/// @brief 语言分段按钮：顺序同样与 core::Language 的枚举值无关。
constexpr core::Language kLanguageOptions[] = {
    core::Language::System,
    core::Language::Chinese,
    core::Language::English,
};

/// @brief 语言按钮上的文字。
///
/// 「跟随系统」是描述，跟着界面语言走；「简体中文」和「English」是语言自称，
/// 任何界面语言下都保持原样，否则英文用户切到中文就看不懂怎么切回去了。
const char* LanguageLabel(core::Language option, core::Language uiLanguage) {
    switch (option) {
        case core::Language::Chinese:
            return "简体中文";
        case core::Language::English:
            return "English";
        case core::Language::System:
            break;
    }
    return Tr(uiLanguage, Text::OptionSystem);
}

ImVec4 ToVec4(ImU32 color) {
    return ImGui::ColorConvertU32ToFloat4(color);
}

ImU32 WithAlpha(ImU32 color, ImU32 alpha) {
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

void FromRgb(uint32_t rgb, float (&out)[3]) {
    out[0] = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
    out[1] = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
    out[2] = static_cast<float>(rgb & 0xFF) / 255.0f;
}

uint32_t ToRgb(const float (&in)[3]) {
    const auto channel = [](float value) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
    };
    return (channel(in[0]) << 16) | (channel(in[1]) << 8) | channel(in[2]);
}

}  // namespace

float SettingsView::TitleBarHeight() {
    return kTitleBarHeight;
}

void SettingsView::Open(const core::Settings& settings, const ActiveHotkeys& active) {
    opened_ = settings;
    working_ = settings;
    active_ = active;
    recording_ = RecordingTarget::None;
    cancelRequested_ = false;
    notice_.clear();
    error_.clear();
}

void SettingsView::SetNotice(const char* notice) {
    notice_ = (notice != nullptr) ? notice : "";
}

void SettingsView::SetError(const char* error) {
    error_ = (error != nullptr) ? error : "";
}

bool SettingsView::OnKeyDown(UINT virtualKey, UINT modifiers) {
    if (recording_ == RecordingTarget::None) {
        if (virtualKey == VK_ESCAPE) {
            cancelRequested_ = true;
            return true;
        }
        return false;
    }

    switch (virtualKey) {
        case VK_ESCAPE:
            // Esc 是录制态的退出键，顺手把它从候选组合里排除，避免录成 Ctrl+Alt+Esc。
            recording_ = RecordingTarget::None;
            error_.clear();
            return true;
        case VK_CONTROL:
        case VK_SHIFT:
        case VK_MENU:
        case VK_LWIN:
        case VK_RWIN:
            // 只有修饰键，继续等真正的按键。
            return true;
        default:
            break;
    }

    core::HotkeyBinding binding;
    binding.modifiers = modifiers;
    binding.virtualKey = virtualKey;
    if (!core::IsValidHotkey(binding)) {
        error_ = Tr(language_, Text::ErrorHotkeyNoModifier);
        return true;
    }

    // 两组热键不能撞在一起：同一个组合键注册两次，后注册的那个必然失败。
    const core::HotkeyBinding& other =
        (recording_ == RecordingTarget::Toggle) ? working_.ignoreHotkey : working_.toggleHotkey;
    if (binding == other) {
        error_ = Tr(language_, Text::ErrorHotkeySame);
        return true;
    }

    if (recording_ == RecordingTarget::Toggle) {
        working_.toggleHotkey = binding;
    } else {
        working_.ignoreHotkey = binding;
    }
    recording_ = RecordingTarget::None;
    error_.clear();
    return true;
}

SettingsView::Action SettingsView::Draw() {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 size = io.DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Px(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ToVec4(theme_.barBackground));
    ImGui::PushStyleColor(ImGuiCol_Border, ToVec4(theme_.barEdge));

    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus;

    Action action = Action::None;
    bool requestClose = false;
    if (ImGui::Begin("##wind_settings", nullptr, kFlags)) {
        DrawTitleBar(requestClose);

        const float left = Px(kPaddingX);
        float nextTop = DrawGeneralSection(left, Px(kTitleBarHeight) + Px(kPaddingY));
        nextTop = DrawIgnoreSection(left, nextTop);
        DrawAppearanceSection(left, nextTop);

        action = DrawFooter(size.x, size.y);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    if (requestClose) {
        action = Action::Cancel;
    }
    if (cancelRequested_) {
        cancelRequested_ = false;
        action = Action::Cancel;
    }
    return action;
}

void SettingsView::DrawTitleBar(bool& requestClose) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float height = Px(kTitleBarHeight);

    draw->AddLine(ImVec2(0.0f, height), ImVec2(ImGui::GetIO().DisplaySize.x, height), theme_.barEdge, 1.0f);

    const char* title = Tr(language_, (recording_ != RecordingTarget::None) ? Text::SettingsTitleRecording
                                                                           : Text::SettingsTitle);
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    draw->AddText(ImVec2(Px(kPaddingX), (height - titleSize.y) * 0.5f), theme_.text, title);

    // 关闭按钮：自绘图标，命中测试仍然交给 ImGui，免得和标题条拖动冲突。
    const float buttonSize = Px(26.0f);
    const float buttonY = (height - buttonSize) * 0.5f;
    const float buttonX = ImGui::GetIO().DisplaySize.x - Px(kPaddingX) - buttonSize;
    ImGui::SetCursorPos(ImVec2(buttonX, buttonY));
    const bool pressed = ImGui::InvisibleButton("##close", ImVec2(buttonSize, buttonSize));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        draw->AddRectFilled(ImVec2(buttonX, buttonY), ImVec2(buttonX + buttonSize, buttonY + buttonSize),
                            theme_.segmentHover, Px(6.0f));
    }
    const ImU32 color = hovered ? theme_.text : theme_.dimText;
    const ImTextureID cross = (icons_ != nullptr) ? icons_->Texture(IconSet::Id::Close) : 0;
    if (cross != 0) {
        // PNG 里带 1024 视口的留白，图形只占约 62%，所以画布比视觉尺寸略大。
        const float crossSize = Px(18.0f);
        const float crossX = std::floor(buttonX + (buttonSize - crossSize) * 0.5f + 0.5f);
        const float crossY = std::floor(buttonY + (buttonSize - crossSize) * 0.5f + 0.5f);
        draw->AddImage(cross, ImVec2(crossX, crossY), ImVec2(crossX + crossSize, crossY + crossSize), ImVec2(0.0f, 0.0f),
                       ImVec2(1.0f, 1.0f), color);
    } else {
        const ImVec2 crossSize = ImGui::CalcTextSize("x");
        draw->AddText(ImVec2(buttonX + (buttonSize - crossSize.x) * 0.5f, buttonY + (buttonSize - crossSize.y) * 0.5f),
                      color, "x");
    }
    if (pressed) {
        requestClose = true;
    }
}

void SettingsView::DrawSectionTitle(const char* text, float left, float top) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddText(ImVec2(left, top), theme_.dimText, text);
}

void SettingsView::DrawLabel(const char* text, float left, float top) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 size = ImGui::CalcTextSize(text);
    draw->AddText(ImVec2(left, top + (Px(kRowHeight) - size.y) * 0.5f), theme_.text, text);
}

bool SettingsView::DrawTextButton(const char* id, const char* label, float x, float y, float width, bool selected,
                                  bool primary) {
    ImGui::SetCursorPos(ImVec2(x, y));
    const ImVec2 size(width, Px(kRowHeight));
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float rounding = Px(6.0f);

    const bool filled = primary || selected;
    if (filled) {
        draw->AddRectFilled(min, max, theme_.accent, rounding);
        if (hovered) {
            draw->AddRectFilled(min, max, WithAlpha(theme_.onAccent, 34), rounding);
        }
    } else {
        draw->AddRect(min, max, theme_.barEdge, rounding);
        if (hovered) {
            draw->AddRectFilled(min, max, theme_.segmentHover, rounding);
        }
    }

    const ImU32 textColor = filled ? theme_.onAccent : theme_.text;
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f), textColor,
                  label);
    return pressed;
}

bool SettingsView::DrawSwitch(const char* id, float x, float y, bool& value) {
    const float width = Px(42.0f);
    const float height = Px(22.0f);
    const float top = y + (Px(kRowHeight) - height) * 0.5f;

    ImGui::SetCursorPos(ImVec2(x, top));
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    if (pressed) {
        value = !value;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min(x, top);
    const ImVec2 max(x + width, top + height);
    const float rounding = height * 0.5f;

    draw->AddRectFilled(min, max, value ? theme_.accent : theme_.barEdge, rounding);
    if (hovered) {
        draw->AddRectFilled(min, max, WithAlpha(value ? theme_.onAccent : theme_.text, 24), rounding);
    }

    // 滑块两端各留 2 px 内缩，靠边时正好与胶囊的半圆同心，看起来才不歪。
    const float inset = Px(2.0f);
    const float knob = height - inset * 2.0f;
    const float knobX = value ? (max.x - inset - knob * 0.5f) : (min.x + inset + knob * 0.5f);
    draw->AddCircleFilled(ImVec2(knobX, top + height * 0.5f), knob * 0.5f,
                          value ? ContrastOn(theme_.accent) : theme_.dimText);
    return pressed;
}

float SettingsView::DrawHintLine(const char* text, ImU32 color, float x, float y, float wrapRight) {
    const float limit = (wrapRight > 0.0f) ? wrapRight : (ImGui::GetWindowWidth() - Px(kPaddingX));
    ImGui::SetCursorPos(ImVec2(x, y));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(color));
    ImGui::PushTextWrapPos(limit);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    return ImGui::GetCursorPosY() + Px(2.0f);
}

bool SettingsView::HasPendingHotkey() const {
    return working_.toggleHotkey != active_.toggle || working_.ignoreHotkey != active_.ignorePicker;
}

float SettingsView::DrawGeneralSection(float left, float top) {
    DrawSectionTitle(Tr(language_, Text::SectionGeneral), left, top);
    const float rowTop = top + Px(20.0f);
    const float controlX = left + Px(kLabelWidth);
    const float resetX = controlX + Px(kHotkeyWidth) + Px(kControlGap);

    // 选取快捷键：录制态由 RecordingTarget::Toggle 标记。
    DrawLabel(Tr(language_, Text::LabelPickHotkey), left, rowTop);
    const bool recordingToggle = recording_ == RecordingTarget::Toggle;
    if (DrawTextButton("##hotkey_toggle",
                       recordingToggle ? Tr(language_, Text::ButtonRecording)
                                       : core::FormatHotkey(working_.toggleHotkey).c_str(),
                       controlX, rowTop, Px(kHotkeyWidth), recordingToggle, false)) {
        recording_ = RecordingTarget::Toggle;
        error_.clear();
    }
    if (DrawTextButton("##hotkey_toggle_reset", Tr(language_, Text::ButtonDefault), resetX, rowTop, Px(kResetWidth),
                       false, false)) {
        working_.toggleHotkey = core::kDefaultSettings.toggleHotkey;
        recording_ = RecordingTarget::None;
        error_.clear();
    }

    const float ignoreTop = rowTop + Px(kRowHeight) + Px(kLineGap);
    DrawLabel(Tr(language_, Text::LabelIgnoreHotkey), left, ignoreTop);
    const bool recordingIgnore = recording_ == RecordingTarget::Ignore;
    if (DrawTextButton("##hotkey_ignore",
                       recordingIgnore ? Tr(language_, Text::ButtonRecording)
                                       : core::FormatHotkey(working_.ignoreHotkey).c_str(),
                       controlX, ignoreTop, Px(kHotkeyWidth), recordingIgnore, false)) {
        recording_ = RecordingTarget::Ignore;
        error_.clear();
    }
    if (DrawTextButton("##hotkey_ignore_reset", Tr(language_, Text::ButtonDefault), resetX, ignoreTop, Px(kResetWidth),
                       false, false)) {
        working_.ignoreHotkey = core::kDefaultSettings.ignoreHotkey;
        recording_ = RecordingTarget::None;
        error_.clear();
    }

    // 提示 / 警告 / 错误三选一，占用固定高度避免下方区块上下跳动。
    float lineTop = ignoreTop + Px(kRowHeight) + Px(kLineGap);

    if (!error_.empty()) {
        lineTop = DrawHintLine(error_.c_str(), theme_.dangerText, controlX, lineTop);
    } else if (!notice_.empty()) {
        lineTop = DrawHintLine(notice_.c_str(), theme_.warnText, controlX, lineTop);
    } else if (!active_.toggleAvailable || !active_.ignorePickerAvailable) {
        // 只说清是哪一组不可用：两组都挂了才用笼统文案。
        Text hint = Text::HintHotkeyUnavailableAny;
        if (!active_.toggleAvailable && active_.ignorePickerAvailable) {
            hint = Text::HintPickHotkeyUnavailable;
        } else if (active_.toggleAvailable && !active_.ignorePickerAvailable) {
            hint = Text::HintIgnoreHotkeyUnavailable;
        }
        lineTop = DrawHintLine(Tr(language_, hint), theme_.warnText, controlX, lineTop);
    } else {
        lineTop = DrawHintLine(Tr(language_, Text::HintHotkeyRecord), theme_.dimText, controlX, lineTop);
    }
    if (HasPendingHotkey()) {
        lineTop = DrawHintLine(Tr(language_, Text::HintAppliesOnConfirm), theme_.dimText, controlX, lineTop);
    }

    // 开机自启：真值在注册表里，这里改的只是工作副本，点「确定」才会真正写入。
    const float autoTop = lineTop + Px(kLineGap);
    DrawLabel(Tr(language_, Text::LabelAutoStart), left, autoTop);
    DrawSwitch("##auto_start", controlX, autoTop, working_.autoStart);
    const char* autoHint = Tr(language_, Text::HintAutoStart);
    const ImVec2 hintSize = ImGui::CalcTextSize(autoHint);
    ImGui::GetWindowDrawList()->AddText(ImVec2(controlX + Px(52.0f), autoTop + (Px(kRowHeight) - hintSize.y) * 0.5f),
                                        theme_.dimText, autoHint);

    // 语言：即时生效，与主题一样走工作副本预览。
    const float languageTop = autoTop + Px(kRowHeight) + Px(kLineGap);
    DrawLabel(Tr(language_, Text::LabelLanguage), left, languageTop);
    DrawLanguageSegments(controlX, languageTop);

    return languageTop + Px(kRowHeight) + Px(kSectionGap);
}

float SettingsView::DrawIgnoreSection(float left, float top) {
    DrawSectionTitle(Tr(language_, Text::SectionIgnore), left, top);

    const float width = ImGui::GetIO().DisplaySize.x - left * 2.0f;
    // 标题右侧的解释文字：说明规则粒度，否则用户会奇怪为什么删了类名还能选到别的窗口。
    const char* caption = Tr(language_, Text::CaptionIgnoreList);
    const ImVec2 captionSize = ImGui::CalcTextSize(caption);
    ImGui::GetWindowDrawList()->AddText(ImVec2(left + width - captionSize.x, top), theme_.dimText, caption);

    const float listTop = top + Px(20.0f);
    const float listHeight = Px(kIgnoreListHeight);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRect(ImVec2(left, listTop), ImVec2(left + width, listTop + listHeight), theme_.barEdge, Px(6.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Px(6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(8.0f), Px(4.0f)));
    ImGui::SetCursorPos(ImVec2(left, listTop));
    ImGui::BeginChild("##ignore_list", ImVec2(width, listHeight), false, ImGuiWindowFlags_NoSavedSettings);

    const float rowHeight = Px(kIgnoreRowHeight);
    const float removeWidth = Px(kIgnoreRemoveWidth);
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    size_t removeIndex = static_cast<size_t>(-1);

    if (working_.ignoredWindows.empty()) {
        const char* empty = Tr(language_, Text::IgnoreListEmpty);
        const float offset = (listHeight - ImGui::GetTextLineHeight()) * 0.5f - Px(4.0f);
        ImGui::SetCursorPos(ImVec2(Px(12.0f), std::max(0.0f, offset)));
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(theme_.dimText));
        ImGui::TextUnformatted(empty);
        ImGui::PopStyleColor();
    } else {
        for (size_t index = 0; index < working_.ignoredWindows.size(); ++index) {
            const core::IgnoreRule& rule = working_.ignoredWindows[index];
            ImGui::PushID(static_cast<int>(index));
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const ImVec2 rowMax(rowMin.x + contentWidth, rowMin.y + rowHeight);

            // 行分隔与悬停底色：列表滚动时也能看清每行的边界。
            if (index > 0) {
                draw->AddLine(ImVec2(rowMin.x, rowMin.y), ImVec2(rowMax.x, rowMin.y), theme_.barEdge, 1.0f);
            }

            ImGui::SetCursorScreenPos(
                ImVec2(rowMax.x - removeWidth, rowMin.y + (rowHeight - Px(20.0f)) * 0.5f));
            const bool removed = ImGui::InvisibleButton("##remove", ImVec2(removeWidth, Px(20.0f)));
            const bool hovered = ImGui::IsItemHovered();
            if (removed) {
                removeIndex = index;
            }

            const ImU32 removeColor = hovered ? theme_.dangerText : theme_.dimText;
            const char* removeLabel = Tr(language_, Text::ButtonRemove);
            const ImVec2 removeSize = ImGui::CalcTextSize(removeLabel);
            draw->AddText(ImVec2(rowMax.x - removeWidth + (removeWidth - removeSize.x) * 0.5f,
                                 rowMin.y + (rowHeight - removeSize.y) * 0.5f),
                          removeColor, removeLabel);

            const std::string label = platform::Narrow(rule.Describe());
            const float labelMax = rowMax.x - removeWidth - Px(8.0f);
            draw->PushClipRect(rowMin, ImVec2(labelMax, rowMax.y), true);
            const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
            draw->AddText(ImVec2(rowMin.x, rowMin.y + (rowHeight - labelSize.y) * 0.5f), theme_.text, label.c_str());
            draw->PopClipRect();

            // 光标挪到下一行行首后必须再提交一个 item，否则 ImGui 会判定这里用
            // SetCursorScreenPos 撑大了窗口边界。
            ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMax.y));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    // 删规则只动工作副本，点「确定」才落盘；这里先记住要删的那一条。
    if (removeIndex != static_cast<size_t>(-1)) {
        working_.ignoredWindows.erase(working_.ignoredWindows.begin() +
                                      static_cast<std::ptrdiff_t>(removeIndex));
    }

    float lineTop = listTop + listHeight + Px(kLineGap);
    lineTop = DrawHintLine(Tr(language_, Text::HintIgnoreHowToAdd), theme_.dimText, left, lineTop);

    return lineTop + Px(kSectionGap);
}

/// @brief 分段按钮的宽度：跟着文案量出来，英文标签比中文长，固定宽度会溢出边框。
float SettingsView::SegmentWidth(const char* label) const {
    return std::max(Px(88.0f), ImGui::CalcTextSize(label).x + Px(24.0f));
}

void SettingsView::DrawThemeSegments(float x, float y) {
    constexpr int kCount = static_cast<int>(std::size(kThemeOptions));
    float cursor = x;
    for (int i = 0; i < kCount; ++i) {
        const core::ThemeMode option = kThemeOptions[i];
        char id[16]{};
        ::snprintf(id, sizeof(id), "##theme%d", i);
        Text label = Text::OptionLight;
        if (option == core::ThemeMode::Dark) {
            label = Text::OptionDark;
        } else if (option == core::ThemeMode::System) {
            label = Text::OptionSystem;
        }
        const char* text = Tr(language_, label);
        const float width = SegmentWidth(text);
        const bool selected = working_.themeMode == option;
        if (DrawTextButton(id, text, cursor, y, width, selected, false)) {
            working_.themeMode = option;
        }
        cursor += width + Px(kControlGap);
    }
}

void SettingsView::DrawLanguageSegments(float x, float y) {
    constexpr int kCount = static_cast<int>(std::size(kLanguageOptions));
    float cursor = x;
    for (int i = 0; i < kCount; ++i) {
        const core::Language option = kLanguageOptions[i];
        char id[16]{};
        ::snprintf(id, sizeof(id), "##language%d", i);
        const char* text = LanguageLabel(option, language_);
        const float width = SegmentWidth(text);
        const bool selected = working_.language == option;
        if (DrawTextButton(id, text, cursor, y, width, selected, false)) {
            working_.language = option;
        }
        cursor += width + Px(kControlGap);
    }
}

float SettingsView::DrawAppearanceSection(float left, float top) {
    DrawSectionTitle(Tr(language_, Text::SectionAppearance), left, top);
    const float rowTop = top + Px(20.0f);
    const float controlX = left + Px(kLabelWidth);

    DrawLabel(Tr(language_, Text::LabelTheme), left, rowTop);
    DrawThemeSegments(controlX, rowTop);

    const float accentTop = rowTop + Px(kRowHeight) + Px(kSectionGap);
    DrawLabel(Tr(language_, Text::LabelAccent), left, accentTop);

    // 取色器绑定的是浮点缓存；未自定义时用主题自带强调色作为初始值。
    if (working_.accentRgb != 0) {
        FromRgb(working_.accentRgb, accentColor_);
    } else {
        const ImVec4 fallback = ToVec4(theme_.accent);
        accentColor_[0] = fallback.x;
        accentColor_[1] = fallback.y;
        accentColor_[2] = fallback.z;
    }

    ImGui::SetCursorPos(ImVec2(controlX, accentTop));
    const ImVec4 swatch(accentColor_[0], accentColor_[1], accentColor_[2], 1.0f);
    if (ImGui::ColorButton("##accent_swatch", swatch, ImGuiColorEditFlags_NoTooltip,
                           ImVec2(Px(64.0f), Px(kRowHeight)))) {
        ImGui::OpenPopup("##accent_popup");
    }
    if (ImGui::BeginPopup("##accent_popup")) {
        if (ImGui::ColorPicker3("##accent_picker", accentColor_,
                                ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview)) {
            working_.accentRgb = ToRgb(accentColor_);
        }
        ImGui::EndPopup();
    }

    const float defaultX = controlX + Px(64.0f) + Px(8.0f);
    if (working_.accentRgb != 0 &&
        DrawTextButton("##accent_default", Tr(language_, Text::ButtonResetAccent), defaultX, accentTop, Px(84.0f),
                       false, false)) {
        working_.accentRgb = 0;
        const ImVec4 fallback = ToVec4(theme_.accent);
        accentColor_[0] = fallback.x;
        accentColor_[1] = fallback.y;
        accentColor_[2] = fallback.z;
    }

    return accentTop + Px(kRowHeight) + Px(kSectionGap);
}

SettingsView::Action SettingsView::DrawFooter(float width, float height) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float footerTop = height - Px(kFooterHeight);
    draw->AddLine(ImVec2(0.0f, footerTop), ImVec2(width, footerTop), theme_.barEdge, 1.0f);

    // 按要求放在左下角：确定在左，取消紧随其后。
    const float buttonY = footerTop + (Px(kFooterHeight) - Px(kRowHeight)) * 0.5f;
    if (DrawTextButton("##confirm", Tr(language_, Text::ButtonConfirm), Px(kPaddingX), buttonY, Px(kButtonWidth), false,
                       true)) {
        return Action::Confirm;
    }
    if (DrawTextButton("##cancel", Tr(language_, Text::ButtonCancel), Px(kPaddingX) + Px(kButtonWidth) + Px(10.0f),
                       buttonY, Px(kButtonWidth), false, false)) {
        return Action::Cancel;
    }
    return Action::None;
}

}  // namespace wind::ui
