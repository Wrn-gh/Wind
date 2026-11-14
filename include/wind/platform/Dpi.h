#pragma once

#include <windows.h>

namespace wind::platform {

/// @brief 把进程声明为 Per-Monitor V2 DPI 感知，必须在创建任何窗口之前调用。
/// 失败时（例如应用清单已声明 DPI 感知）只记录日志，由清单决定最终行为。
void EnablePerMonitorDpiAwareness();

/// @brief 所有显示器并集的物理像素矩形；多显示器布局可能包含负坐标。
/// @return 虚拟屏幕边界，宽度/高度 = right-left / bottom-top。
RECT VirtualScreenBounds();

/// @brief 窗口所在显示器的 DPI 缩放比例（96 DPI 对应 1.0）。
///
/// 进程是 DPI 感知的，ImGui 的绘制尺寸因此全部是物理像素；
/// 界面元素需要用这个比例放大，否则在 150%/200% 缩放下会显得很小。
float WindowDpiScale(HWND window);

/// @brief 主显示器工作区（已扣除任务栏），物理像素；用于避开任务栏摆放提示条。
RECT WorkAreaBounds();

/// @brief 指定显示器的工作区（已扣除任务栏），物理像素。
/// @param monitor 显示器句柄；传 nullptr 时等价于 WorkAreaBounds()。
RECT MonitorWorkArea(HMONITOR monitor);

/// @brief 指定显示器的 DPI 缩放比例（96 DPI 对应 1.0）。
/// @param monitor 显示器句柄；传 nullptr 时返回主显示器的缩放比例。
float MonitorDpiScale(HMONITOR monitor);

}  // namespace wind::platform
