#include "wind/core/TargetWindow.h"

#include <dwmapi.h>

#include <iterator>

namespace wind::core {
namespace {

/// 桌面、任务栏等外壳窗口不应该成为调节对象。
bool IsShellWindow(const wchar_t* className) {
    static const wchar_t* const kSkipped[] = {
        L"Progman",
        L"WorkerW",
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"Windows.UI.Core.CoreWindow",
        L"ForegroundStaging",
    };
    for (const wchar_t* skipped : kSkipped) {
        if (::wcscmp(className, skipped) == 0) {
            return true;
        }
    }
    return false;
}

RECT QueryBounds(HWND hwnd) {
    RECT bounds{};
    const HRESULT hr = ::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds));
    if (FAILED(hr) || ::IsRectEmpty(&bounds) != FALSE) {
        ::GetWindowRect(hwnd, &bounds);
    }
    return bounds;
}

bool IsEligible(HWND hwnd) {
    if (::IsWindowVisible(hwnd) == FALSE || ::IsIconic(hwnd) != FALSE) {
        return false;
    }

    // 被 DWM 隐形（UWP 挂起、虚拟桌面切换）的窗口不应被命中。
    BOOL cloaked = FALSE;
    if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != FALSE) {
        return false;
    }

    wchar_t className[128]{};
    ::GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    if (IsShellWindow(className)) {
        return false;
    }

    const RECT bounds = QueryBounds(hwnd);
    // 过小的窗口多半是隐藏的辅助窗口，误中会让人困惑。
    return (bounds.right - bounds.left) >= 8 && (bounds.bottom - bounds.top) >= 8;
}

}  // namespace

TargetWindow TargetWindow::FindAt(POINT screenPoint, HWND exclude) {
    // 按 Z 序从最上层开始枚举，第一个包含光标的用户窗口就是视觉上被点到的窗口。
    for (HWND hwnd = ::GetTopWindow(nullptr); hwnd != nullptr; hwnd = ::GetWindow(hwnd, GW_HWNDNEXT)) {
        if (hwnd == exclude || !IsEligible(hwnd)) {
            continue;
        }
        const RECT bounds = QueryBounds(hwnd);
        if (::PtInRect(&bounds, screenPoint) != FALSE) {
            return TargetWindow(hwnd);
        }
    }
    return TargetWindow();
}

RECT TargetWindow::Bounds() const {
    if (!IsValid()) {
        return RECT{};
    }
    return QueryBounds(hwnd_);
}

std::wstring TargetWindow::Title() const {
    if (!IsValid()) {
        return {};
    }
    const int length = ::GetWindowTextLengthW(hwnd_);
    if (length <= 0) {
        return {};
    }
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int copied = ::GetWindowTextW(hwnd_, title.data(), length + 1);
    title.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
    return title;
}

DWORD TargetWindow::ProcessId() const {
    if (!IsValid()) {
        return 0;
    }
    DWORD processId = 0;
    ::GetWindowThreadProcessId(hwnd_, &processId);
    return processId;
}

}  // namespace wind::core
