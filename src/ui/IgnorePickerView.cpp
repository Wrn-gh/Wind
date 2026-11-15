#include "wind/ui/IgnorePickerView.h"

#include <imgui.h>

#include "wind/ui/HintBar.h"

namespace wind::ui {
namespace {

constexpr float kFrameThickness = 2.0f;
constexpr float kShadowThickness = 1.0f;
constexpr float kFrameRounding = 2.0f;
/// 光晕的透明度：太高会盖住窗口内容，看不清自己点的是哪个窗口。
constexpr int kGlowAlpha = 40;

}  // namespace

void IgnorePickerView::Draw(const RECT& bounds, POINT origin, const char* hint) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    if (bounds.right > bounds.left && bounds.bottom > bounds.top) {
        const ImVec2 min(static_cast<float>(bounds.left - origin.x), static_cast<float>(bounds.top - origin.y));
        const ImVec2 max(static_cast<float>(bounds.right - origin.x), static_cast<float>(bounds.bottom - origin.y));

        // 先铺内侧光晕，再压描边，这样描边永远是最外层的实线。
        const ImU32 glow = (theme_.frameColor & 0x00FFFFFFu) | (static_cast<ImU32>(kGlowAlpha) << IM_COL32_A_SHIFT);
        draw->AddRectFilled(min, max, glow, Px(kFrameRounding));

        draw->AddRect(ImVec2(min.x - Px(kShadowThickness), min.y - Px(kShadowThickness)),
                      ImVec2(max.x + Px(kShadowThickness), max.y + Px(kShadowThickness)), theme_.frameShadow,
                      Px(kFrameRounding), 0, Px(kShadowThickness));
        draw->AddRect(min, max, theme_.frameColor, Px(kFrameRounding), 0, Px(kFrameThickness));
    }

    DrawHintBar(theme_, uiScale_, origin, hint);
}

}  // namespace wind::ui
