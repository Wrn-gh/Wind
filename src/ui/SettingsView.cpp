#include "wind/ui/SettingsView.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

#include "wind/core/Settings.h"
#include "wind/ui/IconSet.h"

namespace wind::ui {
namespace {

// 96 DPI 下的设计尺寸；实际像素由 uiScale_ 换算。
constexpr float kTitleBarHeight = 40.0f;
constexpr float kPaddingX = 20.0f;
constexpr float kPaddingY = 14.0f;
constexpr float kLabelWidth = 84.0f;
constexpr float kRowHeight = 30.0f;
constexpr float kButtonWidth = 90.0f;
constexpr float kFooterHeight = 60.0f;
constexpr float kSectionGap = 22.0f;
constexpr float kLineGap = 6.0f;

constexpr const char* kHotkeyHint = "点击按钮后按下新的组合键，Esc 取消录制";

/// @brief 主题分段按钮。顺序按界面习惯排列，不依赖 core::ThemeMode 的枚举值顺序。
struct ThemeOption {
    core::ThemeMode mode;
    const char* label;
};
constexpr ThemeOption kThemeOptions[] = {
    {core::ThemeMode::Light, "亮色"},
    {core::ThemeMode::Dark, "暗色"},
    {core::ThemeMode::System, "跟随系统"},
};

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

void SettingsView::Open(const core::Settings& settings, const core::Settings& activeBinding,
                        bool hotkeyAvailable) {
    opened_ = settings;
    working_ = settings;
    active_ = activeBinding;
    hotkeyAvailable_ = hotkeyAvailable;
    recording_ = false;
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
    if (!recording_) {
        if (virtualKey == VK_ESCAPE) {
            cancelRequested_ = true;
            return true;
        }
        return false;
    }

    switch (virtualKey) {
        case VK_ESCAPE:
            // Esc 是录制态的退出键，顺手把它从候选组合里排除，避免录成 Ctrl+Alt+Esc。
            recording_ = false;
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
        error_ = "快捷键至少要包含一个 Ctrl / Alt / Shift / Win 修饰键。";
        return true;
    }

    working_.toggleHotkey = binding;
    recording_ = false;
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
        const float nextTop = DrawGeneralSection(left, Px(kTitleBarHeight) + Px(kPaddingY));
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

    const char* title = recording_ ? "设置 · 录制快捷键" : "设置";
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

float SettingsView::DrawGeneralSection(float left, float top) {
    DrawSectionTitle("常规", left, top);
    const float rowTop = top + Px(20.0f);
    const float controlX = left + Px(kLabelWidth);

    DrawLabel("快捷键", left, rowTop);

    const std::string bindingText = recording_ ? "按下组合键…" : core::FormatHotkey(working_.toggleHotkey);
    if (DrawTextButton("##hotkey", bindingText.c_str(), controlX, rowTop, Px(190.0f), recording_, false)) {
        recording_ = true;
        error_.clear();
    }

    const float resetX = controlX + Px(190.0f) + Px(8.0f);
    if (DrawTextButton("##hotkey_reset", "默认", resetX, rowTop, Px(60.0f), false, false)) {
        working_.toggleHotkey = core::kDefaultSettings.toggleHotkey;
        recording_ = false;
        error_.clear();
    }

    // 提示 / 警告 / 错误三选一，占用固定高度避免下方区块上下跳动。
    float lineTop = rowTop + Px(kRowHeight) + Px(kLineGap);
    const float lineHeight = ImGui::GetTextLineHeight();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (!error_.empty()) {
        draw->AddText(ImVec2(controlX, lineTop), theme_.dangerText, error_.c_str());
        lineTop += lineHeight + Px(2.0f);
    }
    if (!notice_.empty()) {
        draw->AddText(ImVec2(controlX, lineTop), theme_.warnText, notice_.c_str());
        lineTop += lineHeight + Px(2.0f);
    }
    if (error_.empty() && notice_.empty()) {
        const char* hint = hotkeyAvailable_ ? kHotkeyHint : "当前没有生效的全局热键，请重新设置";
        draw->AddText(ImVec2(controlX, lineTop), theme_.dimText, hint);
        lineTop += lineHeight + Px(2.0f);
    }
    if (working_.toggleHotkey != active_.toggleHotkey) {
        draw->AddText(ImVec2(controlX, lineTop), theme_.dimText, "确定后生效");
        lineTop += lineHeight + Px(2.0f);
    }

    // 开机自启：真值在注册表里，这里改的只是工作副本，点「确定」才会真正写入。
    const float autoTop = lineTop + Px(kLineGap);
    DrawLabel("开机自启", left, autoTop);
    DrawSwitch("##auto_start", controlX, autoTop, working_.autoStart);
    const char* autoHint = "登录 Windows 后自动启动";
    const ImVec2 hintSize = ImGui::CalcTextSize(autoHint);
    draw->AddText(ImVec2(controlX + Px(52.0f), autoTop + (Px(kRowHeight) - hintSize.y) * 0.5f), theme_.dimText,
                  autoHint);

    return autoTop + Px(kRowHeight) + Px(kSectionGap);
}

void SettingsView::DrawThemeSegments(float x, float y) {
    constexpr int kCount = static_cast<int>(std::size(kThemeOptions));
    for (int i = 0; i < kCount; ++i) {
        const ThemeOption& option = kThemeOptions[i];
        char id[16]{};
        ::snprintf(id, sizeof(id), "##theme%d", i);
        const bool selected = working_.themeMode == option.mode;
        if (DrawTextButton(id, option.label, x + Px(static_cast<float>(i) * 96.0f), y, Px(88.0f), selected, false)) {
            working_.themeMode = option.mode;
        }
    }
}

float SettingsView::DrawAppearanceSection(float left, float top) {
    DrawSectionTitle("外观", left, top);
    const float rowTop = top + Px(20.0f);
    const float controlX = left + Px(kLabelWidth);

    DrawLabel("主题", left, rowTop);
    DrawThemeSegments(controlX, rowTop);

    const float accentTop = rowTop + Px(kRowHeight) + Px(kSectionGap);
    DrawLabel("主题色", left, accentTop);

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
        DrawTextButton("##accent_default", "恢复默认", defaultX, accentTop, Px(84.0f), false, false)) {
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
    if (DrawTextButton("##confirm", "确定", Px(kPaddingX), buttonY, Px(kButtonWidth), false, true)) {
        return Action::Confirm;
    }
    if (DrawTextButton("##cancel", "取消", Px(kPaddingX) + Px(kButtonWidth) + Px(10.0f), buttonY, Px(kButtonWidth),
                       false, false)) {
        return Action::Cancel;
    }
    return Action::None;
}

}  // namespace wind::ui
