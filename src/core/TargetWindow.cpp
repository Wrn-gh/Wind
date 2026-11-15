#include "wind/core/TargetWindow.h"

#include <dwmapi.h>

#include <algorithm>
#include <iterator>

#include "wind/core/IgnoreList.h"

namespace wind::core {
namespace {

RECT QueryBounds(HWND hwnd) {
    RECT bounds{};
    const HRESULT hr = ::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds));
    if (FAILED(hr) || ::IsRectEmpty(&bounds) != FALSE) {
        ::GetWindowRect(hwnd, &bounds);
    }
    return bounds;
}

bool IsEligible(HWND hwnd, const IgnoreList* ignore) {
    if (::IsWindowVisible(hwnd) == FALSE || ::IsIconic(hwnd) != FALSE) {
        return false;
    }

    // 被 DWM 隐形（UWP 挂起、虚拟桌面切换）的窗口不应被命中。
    BOOL cloaked = FALSE;
    if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != FALSE) {
        return false;
    }

    const std::wstring className = IgnoreList::WindowClassName(hwnd);
    const std::vector<std::wstring>& skipped = BuiltinExcludedClasses();
    if (std::find(skipped.begin(), skipped.end(), className) != skipped.end()) {
        return false;
    }
    // 用户规则是内置黑名单之上的额外一层：被忽略的窗口在枚举里直接消失，
    // 视觉上就像它不存在，指针移过去时由调用方的 sticky 逻辑保留上一个目标。
    if (ignore != nullptr && ignore->Matches(hwnd)) {
        return false;
    }

    const RECT bounds = QueryBounds(hwnd);
    // 过小的窗口多半是隐藏的辅助窗口，误中会让人困惑。
    return (bounds.right - bounds.left) >= 8 && (bounds.bottom - bounds.top) >= 8;
}

}  // namespace

const std::vector<std::wstring>& BuiltinExcludedClasses() {
    static const std::vector<std::wstring> kBuiltin = {
        L"Progman",
        L"WorkerW",
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"Windows.UI.Core.CoreWindow",
        L"ForegroundStaging",
    };
    return kBuiltin;
}

TargetWindow TargetWindow::FindAt(POINT screenPoint, HWND exclude, const IgnoreList* ignore) {
    // 按 Z 序从最上层开始枚举，第一个包含光标的用户窗口就是视觉上被点到的窗口。
    for (HWND hwnd = ::GetTopWindow(nullptr); hwnd != nullptr; hwnd = ::GetWindow(hwnd, GW_HWNDNEXT)) {
        if (hwnd == exclude || !IsEligible(hwnd, ignore)) {
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

std::wstring TargetWindow::ClassName() const {
    if (!IsValid()) {
        return {};
    }
    return IgnoreList::WindowClassName(hwnd_);
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
