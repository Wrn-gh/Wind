#pragma once

#include <imgui.h>

#include <cstdint>

namespace wind::ui {

/// @brief 叠加层里所有自绘元素使用的配色。
///
/// 工具栏、目标窗口描边、底部提示条都是手绘几何，不经过 ImGui 的默认配色，
/// 因此明暗两套颜色集中在这里，由 Application 按系统主题切换。
struct Theme {
    ImU32 frameColor;      ///< 目标窗口描边。
    ImU32 frameShadow;     ///< 描边外侧的一圈压暗，保证在亮色背景上也能看清。
    ImU32 barBackground;   ///< 工具栏底色。
    ImU32 barEdge;         ///< 工具栏 1px 描边。
    ImU32 segmentHover;    ///< 鼠标悬停的区块。
    ImU32 segmentHeld;     ///< 按下的区块。
    ImU32 accent;          ///< 置顶生效时的区块底色。
    ImU32 onAccent;        ///< 叠在 accent 上的文字与图标。
    ImU32 progress;        ///< 透明度进度条已填充部分。
    ImU32 progressTrack;   ///< 透明度进度条底槽。
    ImU32 text;            ///< 常规文字与图标。
    ImU32 dimText;         ///< 禁用态文字与图标（例如透明度已到底）。
    ImU32 hintBackground;  ///< 底部提示条底色。
    ImU32 hintEdge;        ///< 底部提示条描边。
    ImU32 hintText;        ///< 底部提示条文字。
    ImU32 warnText;        ///< 提示类文字（例如热键已降级），不参与强调色染色。
    ImU32 dangerText;      ///< 错误类文字（例如热键被占用）。
};

inline constexpr Theme kDarkTheme{
    /* frameColor    */ IM_COL32(0, 168, 255, 235),
    /* frameShadow   */ IM_COL32(0, 0, 0, 110),
    /* barBackground */ IM_COL32(30, 32, 36, 242),
    /* barEdge       */ IM_COL32(255, 255, 255, 22),
    /* segmentHover  */ IM_COL32(255, 255, 255, 26),
    /* segmentHeld   */ IM_COL32(255, 255, 255, 44),
    /* accent        */ IM_COL32(0, 150, 240, 235),
    /* onAccent      */ IM_COL32(255, 255, 255, 255),
    /* progress      */ IM_COL32(0, 168, 255, 210),
    /* progressTrack */ IM_COL32(255, 255, 255, 28),
    /* text          */ IM_COL32(234, 237, 242, 255),
    /* dimText       */ IM_COL32(132, 138, 148, 255),
    /* hintBackground*/ IM_COL32(28, 30, 34, 235),
    /* hintEdge      */ IM_COL32(0, 168, 255, 200),
    /* hintText      */ IM_COL32(236, 239, 244, 255),
    /* warnText      */ IM_COL32(240, 180, 80, 255),
    /* dangerText    */ IM_COL32(255, 110, 110, 255),
};

inline constexpr Theme kLightTheme{
    /* frameColor    */ IM_COL32(0, 120, 212, 235),
    /* frameShadow   */ IM_COL32(0, 0, 0, 64),
    /* barBackground */ IM_COL32(250, 250, 252, 245),
    /* barEdge       */ IM_COL32(0, 0, 0, 28),
    /* segmentHover  */ IM_COL32(0, 0, 0, 20),
    /* segmentHeld   */ IM_COL32(0, 0, 0, 38),
    /* accent        */ IM_COL32(0, 120, 212, 240),
    /* onAccent      */ IM_COL32(255, 255, 255, 255),
    /* progress      */ IM_COL32(0, 120, 212, 225),
    /* progressTrack */ IM_COL32(0, 0, 0, 26),
    /* text          */ IM_COL32(26, 28, 32, 255),
    /* dimText       */ IM_COL32(112, 117, 124, 255),
    /* hintBackground*/ IM_COL32(252, 252, 253, 242),
    /* hintEdge      */ IM_COL32(0, 120, 212, 200),
    /* hintText      */ IM_COL32(26, 28, 32, 255),
    /* warnText      */ IM_COL32(176, 106, 0, 255),
    /* dangerText    */ IM_COL32(196, 40, 40, 255),
};

/// @brief 取与给定背景对比更强的前景色：浅底用深字，深底用白字。
///
/// 用户可以把强调色选成浅黄，此时白字会糊成一片，所以叠在强调色上的文字
/// 不能写死成白色，得按背景的相对亮度自动决定。
inline ImU32 ContrastOn(ImU32 background) {
    const float red = static_cast<float>((background >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
    const float green = static_cast<float>((background >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
    const float blue = static_cast<float>((background >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
    const float luminance = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
    return (luminance > 0.55f) ? IM_COL32(20, 22, 26, 255) : IM_COL32(255, 255, 255, 255);
}

/// @brief 用一个 RGB 值替换颜色的色相，保留原有 alpha。
inline ImU32 RecolorKeepingAlpha(ImU32 color, uint32_t rgb) {
    const ImU32 alpha = (color >> IM_COL32_A_SHIFT) & 0xFF;
    return IM_COL32(static_cast<int>((rgb >> 16) & 0xFF), static_cast<int>((rgb >> 8) & 0xFF),
                    static_cast<int>(rgb & 0xFF), static_cast<int>(alpha));
}

/// @brief 按设置拼出最终配色：明暗底表 + 可选自定义强调色。
/// @param light 是否使用亮色底表。
/// @param accentRgb 强调色 0xRRGGBB；传 0 表示沿用主题自带强调色（结果与 kLightTheme /
///        kDarkTheme 完全一致，保证未改过设置的用户观感不变）。
inline Theme MakeTheme(bool light, uint32_t accentRgb) {
    Theme theme = light ? kLightTheme : kDarkTheme;
    if (accentRgb == 0) {
        return theme;
    }
    theme.frameColor = RecolorKeepingAlpha(theme.frameColor, accentRgb);
    theme.accent = RecolorKeepingAlpha(theme.accent, accentRgb);
    theme.progress = RecolorKeepingAlpha(theme.progress, accentRgb);
    theme.hintEdge = RecolorKeepingAlpha(theme.hintEdge, accentRgb);
    theme.onAccent = ContrastOn(theme.accent);
    return theme;
}

}  // namespace wind::ui
