#include "wind/ui/HintBar.h"

#include <imgui.h>

#include <algorithm>

#include "wind/platform/Dpi.h"

namespace wind::ui {
namespace {

/// 与工具栏同一套设计值，改动时请同步 ToolbarView.cpp。
constexpr float kBarRounding = 8.0f;
constexpr float kMargin = 8.0f;
constexpr float kEdgeThickness = 1.0f;
constexpr float kHintPaddingX = 14.0f;
constexpr float kHintPaddingY = 9.0f;

}  // namespace

void DrawHintBar(const Theme& theme, float uiScale, POINT origin, const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }

    const auto px = [uiScale](float value) { return value * (uiScale > 0.0f ? uiScale : 1.0f); };

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 padding(px(kHintPaddingX), px(kHintPaddingY));
    const ImVec2 boxSize(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);

    // 贴着主显示器工作区底部摆放，避免被任务栏挡住（工作区已扣除任务栏）。
    const RECT work = platform::WorkAreaBounds();
    const float workLeft = static_cast<float>(work.left - origin.x);
    const float workBottom = static_cast<float>(work.bottom - origin.y);

    float x = workLeft + (static_cast<float>(work.right - work.left) - boxSize.x) * 0.5f;
    x = std::clamp(x, px(kMargin), std::max(px(kMargin), io.DisplaySize.x - boxSize.x - px(kMargin)));

    float y = workBottom - boxSize.y - px(kMargin) * 2.0f;
    y = std::min(y, io.DisplaySize.y - boxSize.y - px(kMargin));
    y = std::max(y, px(kMargin));

    const ImVec2 min(x, y);
    const ImVec2 max(min.x + boxSize.x, min.y + boxSize.y);

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(min, max, theme.hintBackground, px(kBarRounding));
    draw->AddRect(min, max, theme.hintEdge, px(kBarRounding), 0, px(kEdgeThickness));
    draw->AddText(ImVec2(min.x + padding.x, min.y + padding.y), theme.hintText, text);
}

}  // namespace wind::ui
