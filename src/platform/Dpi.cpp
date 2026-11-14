#include "wind/platform/Dpi.h"

#include <shellscalingapi.h>

#include "wind/platform/Win32Error.h"

namespace wind::platform {

void EnablePerMonitorDpiAwareness() {
    // 运行时声明优先于清单；若进程已由清单决定了 DPI 行为，这里会失败，属于可接受情况。
    if (::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE) {
        return;
    }
    Log("SetProcessDpiAwarenessContext 失败：%s", LastErrorMessage(::GetLastError()).c_str());
}

RECT VirtualScreenBounds() {
    RECT bounds{};
    bounds.left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    bounds.top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    bounds.right = bounds.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    bounds.bottom = bounds.top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return bounds;
}

float WindowDpiScale(HWND window) {
    const UINT dpi = (window != nullptr) ? ::GetDpiForWindow(window) : ::GetDpiForSystem();
    if (dpi == 0) {
        return 1.0f;
    }
    return static_cast<float>(dpi) / 96.0f;
}

RECT WorkAreaBounds() {
    RECT work{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) == FALSE) {
        return VirtualScreenBounds();
    }
    return work;
}

RECT MonitorWorkArea(HMONITOR monitor) {
    if (monitor == nullptr) {
        return WorkAreaBounds();
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetMonitorInfoW(monitor, &info) == FALSE) {
        return WorkAreaBounds();
    }
    return info.rcWork;
}

float MonitorDpiScale(HMONITOR monitor) {
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (monitor != nullptr && SUCCEEDED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX != 0) {
        return static_cast<float>(dpiX) / 96.0f;
    }
    return 1.0f;
}

}  // namespace wind::platform
