#include "wind/ui/ToolbarView.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "wind/platform/Dpi.h"
#include "wind/ui/HintBar.h"
#include "wind/ui/IconSet.h"
#include "wind/ui/Text.h"

namespace wind::ui {
namespace {

/// 目标窗口描边颜色（强调色），以及外侧一圈半透明压暗让描边在浅色背景上也可辨识。
/// 具体色值见 wind/ui/Theme.h，随系统亮/暗主题切换。
/// 以下尺寸都是 96 DPI 下的设计值，绘制时统一乘以 uiScale_ 换算成物理像素。
constexpr float kFrameThickness = 2.0f;
constexpr float kShadowThickness = 1.0f;
constexpr float kFrameRounding = 2.0f;
constexpr float kMargin = 8.0f;
constexpr float kGap = 8.0f;

/// 横条：内边距、圆角与各区块宽度。
constexpr float kBarRounding = 8.0f;
constexpr float kBarPaddingX = 5.0f;
constexpr float kBarPaddingY = 4.0f;
constexpr float kSegmentGap = 3.0f;
constexpr float kSegmentHeight = 28.0f;
constexpr float kSegmentRounding = 6.0f;
constexpr float kPinWidth = 64.0f;
constexpr float kStepWidth = 32.0f;
constexpr float kOpacityWidth = 88.0f;
constexpr float kResetWidth = 52.0f;
/// 有图标时置顶 / 重置区块收窄成图标格，与加减号等宽。
constexpr float kIconWidth = 32.0f;
/// 图标的绘制尺寸（96 DPI 下的设计值）：与 18 px 的字高对齐，
/// 再小图钉的斜向轮廓就糊成一团了。
constexpr float kIconSize = 18.0f;
/// 图标与文字并排时的间距。
constexpr float kIconTextGap = 5.0f;
/// 透明度区块底部进度条的左右内缩与高度。
constexpr float kTrackInset = 5.0f;
constexpr float kTrackHeight = 3.0f;

/// 滚轮 / 按钮单步调整的百分点。
constexpr int kStep = 5;

/// @brief 把百分比换算成 0..1 的比例。
float OpacityRatio(int percent) {
    constexpr int kMin = core::WindowProperties::kMinOpacity;
    const int clamped = std::clamp(percent, kMin, 100);
    return static_cast<float>(clamped - kMin) / static_cast<float>(100 - kMin);
}

/// @brief 把 0..1 的比例换算回百分比（吸附到整数点）。
int RatioToPercent(float ratio) {
    constexpr int kMin = core::WindowProperties::kMinOpacity;
    const float clamped = std::clamp(ratio, 0.0f, 1.0f);
    return kMin + static_cast<int>(std::lround(clamped * static_cast<float>(100 - kMin)));
}

}  // namespace

ToolbarView::Command ToolbarView::Draw(const core::Selection& selection, POINT origin) {
    Command command;
    if (!selection.HasTarget()) {
        screenBounds_ = RECT{};
        barrier_ = RECT{};
        pressedSegment_ = -1;
        DrawHint(origin);
        return command;
    }

    DrawTargetFrame(selection, origin);
    SyncSlider(selection);
    Layout();

    const ImGuiIO& io = ImGui::GetIO();

    // 工具栏默认贴在目标窗口的左上角上方，和窗口左侧对齐；上方没位置就翻到窗口下方，
    // 再不行就压进窗口内侧的左上角，保证始终能看见且不被窗口自身挡住。
    const RECT bounds = selection.TargetBounds();
    const float left = static_cast<float>(bounds.left - origin.x);
    const float top = static_cast<float>(bounds.top - origin.y);
    const float height = static_cast<float>(bounds.bottom - bounds.top);

    float x = left;
    float y = top - barHeight_ - Px(kGap);
    if (y < Px(kMargin)) {
        y = top + height + Px(kGap);
    }
    if (y + barHeight_ > io.DisplaySize.y - Px(kMargin)) {
        // 上下都挤不下时才落到窗口内部的左上角。
        x = left + Px(kMargin);
        y = top + Px(kMargin);
    }
    x = std::clamp(x, Px(kMargin), std::max(Px(kMargin), io.DisplaySize.x - barWidth_ - Px(kMargin)));
    y = std::clamp(y, Px(kMargin), std::max(Px(kMargin), io.DisplaySize.y - barHeight_ - Px(kMargin)));

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(barWidth_, barHeight_), ImGuiCond_Always);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const bool locked = selection.OpacityLocked();

    if (ImGui::Begin("##WindToolbar", nullptr, kFlags)) {
        const ImVec2 barMin = ImGui::GetCursorScreenPos();

        // 整条横条就是一个按钮：分区命中交给 HitSegment 自己算，这样区块能完全平铺，
        // 不会出现一格一格的按钮边框，悬停高亮也能精确到区块。
        ImGui::InvisibleButton("##bar", ImVec2(barWidth_, barHeight_));
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const bool clicked = ImGui::IsItemClicked();

        const ImVec2 mouse = io.MousePos;
        const float localX = mouse.x - barMin.x;
        const float localY = mouse.y - barMin.y;
        const bool inside = hovered && localX >= 0.0f && localY >= 0.0f && localX <= barWidth_ &&
                            localY <= barHeight_;
        const int index = inside ? HitSegment(localX) : -1;

        if (clicked) {
            pressedSegment_ = index;
        }

        // 透明度区块支持按住拖动，语义和拖动滑块一致。
        if (held && pressedSegment_ >= 0 && !locked) {
            const Segment& segment = segments_[static_cast<size_t>(pressedSegment_)];
            if (segment.kind == Segment::Kind::Opacity && segment.width > 0.0f) {
                const float ratio = (localX - Px(kBarPaddingX) - segment.x) / segment.width;
                const int percent = RatioToPercent(ratio);
                if (percent != sliderValue_) {
                    sliderValue_ = percent;
                    command = {Command::Type::SetOpacity, percent};
                }
            }
        }

        // 松开鼠标时才触发：松开位置仍在同一区块才算点击成功。
        if (!held && pressedSegment_ >= 0) {
            if (index == pressedSegment_) {
                Trigger(segments_[static_cast<size_t>(pressedSegment_)], locked, command);
            }
            pressedSegment_ = -1;
        }

        const int hoverIndex = (hovered && index >= 0) ? index : -1;
        const int heldIndex = (held && pressedSegment_ >= 0) ? pressedSegment_ : -1;
        DrawBar(selection, locked, barMin.x, barMin.y, hoverIndex, heldIndex);

        screenBounds_.left = origin.x + static_cast<LONG>(std::lround(barMin.x));
        screenBounds_.top = origin.y + static_cast<LONG>(std::lround(barMin.y));
        screenBounds_.right = screenBounds_.left + static_cast<LONG>(std::lround(barWidth_));
        screenBounds_.bottom = screenBounds_.top + static_cast<LONG>(std::lround(barHeight_));
        UpdateBarrier(bounds, screenBounds_);
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    return command;
}

void ToolbarView::SetUiScale(float scale) {
    uiScale_ = (scale > 0.0f) ? scale : 1.0f;
    Layout();
}

bool ToolbarView::ContainsScreenPoint(POINT screenPoint) const {
    if (screenBounds_.right <= screenBounds_.left || screenBounds_.bottom <= screenBounds_.top) {
        return false;
    }
    // 往外放宽几个像素，鼠标从边缘掠过时不会因为一帧的误差丢掉目标。
    RECT tolerant = screenBounds_;
    const LONG slack = static_cast<LONG>(std::lround(Px(4.0f)));
    tolerant.left -= slack;
    tolerant.top -= slack;
    tolerant.right += slack;
    tolerant.bottom += slack;
    return ::PtInRect(&tolerant, screenPoint) != FALSE;
}

void ToolbarView::UpdateBarrier(const RECT& targetScreen, const RECT& barScreen) {
    barrier_ = barScreen;

    // 工具栏贴在目标窗口外侧，两者之间有一小段空隙。鼠标从窗口内部直线移向工具栏时，
    // 必然要跨过这段空隙并压在别的窗口上；把这段走廊一并纳入保护区，目标才不会被换掉。
    RECT corridor{};
    if (barScreen.bottom <= targetScreen.top) {
        corridor.left = std::min(barScreen.left, targetScreen.left);
        corridor.right = std::max(barScreen.right, targetScreen.right);
        corridor.top = barScreen.bottom;
        corridor.bottom = targetScreen.top;
    } else if (barScreen.top >= targetScreen.bottom) {
        corridor.left = std::min(barScreen.left, targetScreen.left);
        corridor.right = std::max(barScreen.right, targetScreen.right);
        corridor.top = targetScreen.bottom;
        corridor.bottom = barScreen.top;
    } else {
        return;
    }

    RECT merged{};
    ::UnionRect(&merged, &barrier_, &corridor);
    barrier_ = merged;
}

bool ToolbarView::ProtectsPoint(POINT screenPoint) const {
    // 允许几个像素的误差，避免指针从保护区边缘掠过时刚好落空。
    RECT tolerant = barrier_;
    const LONG slack = static_cast<LONG>(std::lround(Px(6.0f)));
    tolerant.left -= slack;
    tolerant.top -= slack;
    tolerant.right += slack;
    tolerant.bottom += slack;
    return ::PtInRect(&tolerant, screenPoint) != FALSE;
}

void ToolbarView::SetIcons(const IconSet* icons) {
    icons_ = icons;
    // 图标可用与否会改变区块宽度，所以立刻重排一次。
    Layout();
}

bool ToolbarView::IconsReady() const {
    return icons_ != nullptr && icons_->Ready();
}

void ToolbarView::Layout() {
    struct Spec {
        Segment::Kind kind;
        float width;
    };
    const bool icons = IconsReady();
    const Spec specs[] = {
        {Segment::Kind::Pin, icons ? kIconWidth : kPinWidth},
        {Segment::Kind::Decrease, kStepWidth},
        {Segment::Kind::Opacity, kOpacityWidth},
        {Segment::Kind::Increase, kStepWidth},
        {Segment::Kind::Reset, icons ? kIconWidth : kResetWidth},
    };

    float x = 0.0f;
    for (size_t i = 0; i < segments_.size(); ++i) {
        segments_[i].kind = specs[i].kind;
        segments_[i].x = x;
        segments_[i].width = Px(specs[i].width);
        x += segments_[i].width + Px(kSegmentGap);
    }
    x -= Px(kSegmentGap);
    barWidth_ = x + Px(kBarPaddingX) * 2.0f;
    barHeight_ = Px(kSegmentHeight) + Px(kBarPaddingY) * 2.0f;
}

int ToolbarView::HitSegment(float localX) const {
    const float contentX = localX - Px(kBarPaddingX);
    for (size_t i = 0; i < segments_.size(); ++i) {
        const Segment& segment = segments_[i];
        if (contentX >= segment.x && contentX < segment.x + segment.width) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void ToolbarView::Trigger(const Segment& segment, bool locked, Command& command) {
    switch (segment.kind) {
        case Segment::Kind::Pin:
            command = {Command::Type::ToggleTopmost, 0};
            break;
        case Segment::Kind::Decrease:
            if (!locked) {
                command = {Command::Type::AdjustOpacity, -kStep};
            }
            break;
        case Segment::Kind::Increase:
            if (!locked) {
                command = {Command::Type::AdjustOpacity, kStep};
            }
            break;
        case Segment::Kind::Reset:
            if (!locked) {
                command = {Command::Type::Reset, 0};
            }
            break;
        case Segment::Kind::Opacity:
            // 透明度区块的值在按下/拖动时已经实时生效，松开时不再重复触发。
            break;
    }
}

void ToolbarView::DrawBar(const core::Selection& selection, bool locked, float left, float top, int hoverIndex,
                          int heldIndex) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const Theme& theme = theme_;

    const ImVec2 barMin(left, top);
    const ImVec2 barMax(left + barWidth_, top + barHeight_);
    draw->AddRectFilled(barMin, barMax, theme.barBackground, Px(kBarRounding));
    draw->AddRect(barMin, barMax, theme.barEdge, Px(kBarRounding), 0, 1.0f);

    const float padding = Px(kBarPaddingX);
    const float segmentTop = top + Px(kBarPaddingY);
    const float segmentBottom = segmentTop + Px(kSegmentHeight);
    const bool pinned = selection.AlwaysOnTop();

    for (size_t i = 0; i < segments_.size(); ++i) {
        const Segment& segment = segments_[i];
        const int index = static_cast<int>(i);
        const bool disabled = locked && segment.kind != Segment::Kind::Pin;

        const ImVec2 segMin(left + padding + segment.x, segmentTop);
        const ImVec2 segMax(segMin.x + segment.width, segmentBottom);

        if (index == heldIndex) {
            draw->AddRectFilled(segMin, segMax, theme.segmentHeld, Px(kSegmentRounding));
        } else if (index == hoverIndex && !disabled) {
            draw->AddRectFilled(segMin, segMax, theme.segmentHover, Px(kSegmentRounding));
        }

        const bool icons = IconsReady();
        const char* label = "";
        IconSet::Id iconId = IconSet::Id::Pin;
        bool useIcon = false;
        ImU32 color = disabled ? theme.dimText : theme.text;
        char percentBuffer[16]{};

        switch (segment.kind) {
            case Segment::Kind::Pin:
                // 置顶态已经用整块强调色表达（accent 填充），图标格不必再写字。
                if (icons) {
                    useIcon = true;
                    iconId = IconSet::Id::Pin;
                } else {
                    label = Tr(language_, pinned ? Text::ToolbarPinned : Text::ToolbarPin);
                }
                if (pinned) {
                    draw->AddRectFilled(segMin, segMax, theme.accent, Px(kSegmentRounding));
                    color = theme.onAccent;
                }
                break;
            case Segment::Kind::Decrease:
                label = "-";
                break;
            case Segment::Kind::Increase:
                label = "+";
                break;
            case Segment::Kind::Opacity:
                if (locked) {
                    label = Tr(language_, Text::ToolbarLocked);
                    break;
                }
                ::snprintf(percentBuffer, sizeof(percentBuffer), "%d%%", sliderValue_);
                label = percentBuffer;
                useIcon = icons;
                iconId = IconSet::Id::Opacity;
                break;
            case Segment::Kind::Reset:
                if (icons) {
                    useIcon = true;
                    iconId = IconSet::Id::Reset;
                } else {
                    label = Tr(language_, Text::ToolbarReset);
                }
                break;
        }

        // 图标是白色蒙版，颜色全靠顶点色相乘，因此和文字用同一套配色就能保持统一。
        const ImTextureID texture = useIcon ? icons_->Texture(iconId) : 0;
        const ImVec2 textSize = (label[0] != '\0') ? ImGui::CalcTextSize(label) : ImVec2(0.0f, 0.0f);
        const float iconSize = (texture != 0) ? Px(kIconSize) : 0.0f;
        const float iconGap = (iconSize > 0.0f && textSize.x > 0.0f) ? Px(kIconTextGap) : 0.0f;
        const float contentWidth = iconSize + iconGap + textSize.x;
        const float contentHeight = std::max(iconSize, textSize.y);

        const bool showTrack = segment.kind == Segment::Kind::Opacity && !locked;
        const float trackLift = showTrack ? Px(kTrackHeight) + Px(kTrackInset) * 0.5f : 0.0f;
        const float contentLeft = std::floor(segMin.x + (segment.width - contentWidth) * 0.5f + 0.5f);
        const float contentTop =
            std::floor(segMin.y + (Px(kSegmentHeight) - contentHeight - trackLift) * 0.5f + 0.5f);
        if (iconSize > 0.0f) {
            const float iconTop = std::floor(segMin.y + (Px(kSegmentHeight) - iconSize - trackLift) * 0.5f + 0.5f);
            draw->AddImage(texture, ImVec2(contentLeft, iconTop), ImVec2(contentLeft + iconSize, iconTop + iconSize),
                           ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), color);
        }
        if (showTrack) {
            // 透明度用底部一条细进度条表示，区块整体仍是普通平铺项，
            // 不会因为数值高就整块变成蓝色“选中态”。
            const float trackHeight = Px(kTrackHeight);
            const float trackWidth = std::min(segment.width - Px(kTrackInset) * 2.0f, textSize.x + Px(24.0f));
            const float trackLeft = std::floor(segMin.x + (segment.width - trackWidth) * 0.5f);
            const float trackTop = segMax.y - Px(kTrackInset) - trackHeight;
            const ImVec2 trackMin(trackLeft, trackTop);
            const ImVec2 trackMax(trackLeft + trackWidth, trackTop + trackHeight);
            draw->AddRectFilled(trackMin, trackMax, theme.progressTrack, trackHeight * 0.5f);
            const float ratio = OpacityRatio(sliderValue_);
            if (ratio > 0.0f) {
                draw->AddRectFilled(trackMin, ImVec2(trackLeft + trackWidth * ratio, trackMax.y), theme.progress,
                                    trackHeight * 0.5f);
            }
        }
        if (textSize.x > 0.0f) {
            draw->AddText(ImVec2(contentLeft + iconSize + iconGap, contentTop), color, label);
        }
    }
}

void ToolbarView::DrawTargetFrame(const core::Selection& selection, POINT origin) {
    const RECT bounds = selection.TargetBounds();
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return;
    }

    const Theme& theme = theme_;
    const ImVec2 min(static_cast<float>(bounds.left - origin.x), static_cast<float>(bounds.top - origin.y));
    const ImVec2 max(static_cast<float>(bounds.right - origin.x), static_cast<float>(bounds.bottom - origin.y));

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddRect(ImVec2(min.x - Px(kShadowThickness), min.y - Px(kShadowThickness)),
                  ImVec2(max.x + Px(kShadowThickness), max.y + Px(kShadowThickness)), theme.frameShadow,
                  Px(kFrameRounding), 0, Px(kShadowThickness));
    draw->AddRect(min, max, theme.frameColor, Px(kFrameRounding), 0, Px(kFrameThickness));
}

void ToolbarView::DrawHint(POINT origin) {
    DrawHintBar(theme_, uiScale_, origin, Tr(language_, Text::HintPickMode));
}

void ToolbarView::SyncSlider(const core::Selection& selection) {
    sliderValue_ = std::clamp(selection.Opacity(), core::WindowProperties::kMinOpacity, 100);
}

}  // namespace wind::ui
