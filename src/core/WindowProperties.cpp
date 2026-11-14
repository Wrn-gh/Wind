#include "wind/core/WindowProperties.h"

#include <algorithm>
#include <cmath>

#include "wind/platform/Win32Error.h"

namespace wind::core {
namespace {

LONG_PTR ReadExStyle(HWND hwnd) {
    return static_cast<LONG_PTR>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
}

/// SetWindowLongPtr 无法用返回值区分“失败”和“旧值为 0”，必须配合 GetLastError。
bool WriteExStyle(HWND hwnd, LONG_PTR style) {
    ::SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
    if (previous == 0 && ::GetLastError() != ERROR_SUCCESS) {
        platform::Log("SetWindowLongPtr(GWL_EXSTYLE) 失败：%s",
                      platform::LastErrorMessage(::GetLastError()).c_str());
        return false;
    }
    return true;
}

}  // namespace

WindowProperties::WindowProperties(HWND hwnd) : hwnd_(hwnd) {
    if (hwnd_ == nullptr) {
        return;
    }

    exStyle_ = ReadExStyle(hwnd_);
    originalTopmost_ = (exStyle_ & WS_EX_TOPMOST) != 0;
    originalLayered_ = (exStyle_ & WS_EX_LAYERED) != 0;
    topmost_ = originalTopmost_;

    if (originalLayered_) {
        DWORD flags = 0;
        BYTE alpha = 255;
        if (::GetLayeredWindowAttributes(hwnd_, nullptr, &alpha, &flags) != FALSE && (flags & LWA_ALPHA) != 0) {
            originalAlpha_ = alpha;
            hasOriginalAlpha_ = true;
            opacity_ = std::clamp(static_cast<int>(std::lround(alpha * 100.0 / 255.0)), kMinOpacity, 100);
        }
    }
}

bool WindowProperties::SetOpacity(int percent) {
    if (!IsValid()) {
        return false;
    }

    const int target = std::clamp(percent, kMinOpacity, 100);
    if (target == opacity_) {
        return false;
    }

    if (target >= 100) {
        if (!RestoreLayeredState()) {
            return false;
        }
        opacity_ = 100;
        return true;
    }

    if (!ApplyAlpha(static_cast<BYTE>(target * 255 / 100))) {
        return false;
    }
    opacity_ = target;
    return true;
}

bool WindowProperties::ApplyAlpha(unsigned char alpha) {
    if ((exStyle_ & WS_EX_LAYERED) == 0) {
        const LONG_PTR style = ReadExStyle(hwnd_);
        if (!WriteExStyle(hwnd_, style | WS_EX_LAYERED)) {
            return false;
        }
        exStyle_ = style | WS_EX_LAYERED;
        ::SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    if (::SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA) == FALSE) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_ACCESS_DENIED) {
            // UIPI：目标权限更高，本进程无权修改其分层属性。
            opacityLocked_ = true;
        }
        platform::Log("SetLayeredWindowAttributes 失败：%s", platform::LastErrorMessage(error).c_str());
        return false;
    }

    opacityLocked_ = false;
    return true;
}

bool WindowProperties::RestoreLayeredState() {
    if (originalLayered_) {
        const BYTE alpha = hasOriginalAlpha_ ? originalAlpha_ : static_cast<BYTE>(255);
        if (::SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA) == FALSE) {
            platform::Log("还原透明度失败：%s", platform::LastErrorMessage(::GetLastError()).c_str());
            return false;
        }
        return true;
    }

    // 原本不是分层窗口：把我们添加的 WS_EX_LAYERED 摘掉，恢复原有视觉行为。
    if ((ReadExStyle(hwnd_) & WS_EX_LAYERED) == 0) {
        return true;
    }
    const LONG_PTR style = ReadExStyle(hwnd_) & ~static_cast<LONG_PTR>(WS_EX_LAYERED);
    if (!WriteExStyle(hwnd_, style)) {
        return false;
    }
    exStyle_ = style;
    ::SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return true;
}

bool WindowProperties::SetAlwaysOnTop(bool enabled) {
    if (!IsValid() || enabled == topmost_) {
        return false;
    }

    const HWND insertAfter = enabled ? HWND_TOPMOST : HWND_NOTOPMOST;
    if (::SetWindowPos(hwnd_, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) == FALSE) {
        platform::Log("SetWindowPos(置顶) 失败：%s", platform::LastErrorMessage(::GetLastError()).c_str());
        return false;
    }

    topmost_ = enabled;
    return true;
}

void WindowProperties::Restore() {
    if (!IsValid()) {
        return;
    }
    SetAlwaysOnTop(originalTopmost_);
    if (RestoreLayeredState()) {
        opacity_ = OriginalOpacity();
    }
}

int WindowProperties::OriginalOpacity() const {
    return (originalLayered_ && hasOriginalAlpha_)
               ? std::clamp(static_cast<int>(std::lround(originalAlpha_ * 100.0 / 255.0)), kMinOpacity, 100)
               : 100;
}

bool WindowProperties::Modified() const {
    return topmost_ != originalTopmost_ || opacity_ != OriginalOpacity();
}

}  // namespace wind::core
