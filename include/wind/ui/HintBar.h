#pragma once

#include <windows.h>

#include "wind/ui/Theme.h"

namespace wind::ui {

/// @brief 在屏幕工作区底部居中画一条圆角提示条。
///
/// 选取模式和忽略窗口模式共用它：两条提示文字不同，但位置、内边距、配色完全一致，
/// 否则用户在两个模式之间切换时会觉得界面“跳”了一下。
///
/// @param theme 当前主题（取 hintBackground / hintEdge / hintText）。
/// @param uiScale 界面缩放系数，内部把 96 DPI 设计值换算成物理像素。
/// @param origin 叠加层窗口左上角的屏幕坐标，用于把工作区坐标转成窗口坐标。
/// @param text UTF-8 文本；传 nullptr 或空串时不绘制。
void DrawHintBar(const Theme& theme, float uiScale, POINT origin, const char* text);

}  // namespace wind::ui
