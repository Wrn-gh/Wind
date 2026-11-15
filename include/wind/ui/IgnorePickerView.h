#pragma once

#include <windows.h>

#include "wind/ui/Theme.h"

namespace wind::ui {

/// @brief 「选取要忽略的窗口」模式的叠加层绘制。
///
/// 视觉上刻意与选取模式区分：只描边、不出工具栏。这个模式点一下就结束，
/// 浮出一条按钮条既没必要，也会挡住用户真正想点的那个角落。
class IgnorePickerView {
public:
    /// @brief 设置配色；与工具栏共用同一个 Theme，保证两个模式观感一致。
    void SetTheme(const Theme& theme) { theme_ = theme; }
    /// @brief 设置界面缩放比例（与渲染器的 DPI 缩放保持一致）。
    void SetUiScale(float scale) { uiScale_ = (scale > 0.0f) ? scale : 1.0f; }

    /// @brief 绘制一帧。
    /// @param bounds 光标本帧命中的窗口边界（屏幕坐标）；宽高为 0 表示没有命中。
    /// @param origin 叠加层客户区左上角对应的屏幕坐标（多显示器时可能为负）。
    /// @param hint 底部提示文字（UTF-8），由应用层按是否有目标决定。
    void Draw(const RECT& bounds, POINT origin, const char* hint);

private:
    /// @brief 把 96 DPI 下的设计尺寸换算成当前 DPI 的物理像素。
    float Px(float value) const { return value * uiScale_; }

    Theme theme_ = kDarkTheme;
    float uiScale_ = 1.0f;
};

}  // namespace wind::ui
