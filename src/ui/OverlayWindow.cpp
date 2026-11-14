#include "wind/ui/OverlayWindow.h"

#include <imgui_impl_win32.h>

#include "wind/platform/Dpi.h"
#include "wind/platform/Win32Error.h"

// imgui_impl_win32.h 故意不在头文件里声明该函数（避免在头文件里包含 <windows.h>），
// 因此这里按官方示例补上前置声明。
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace wind::ui {
namespace {

constexpr wchar_t kClassName[] = L"Wind.OverlayWindow";

}  // namespace

OverlayWindow::~OverlayWindow() {
    Destroy();
}

bool OverlayWindow::Create(HINSTANCE instance, Listener* listener) {
    if (hwnd_ != nullptr) {
        return true;
    }
    if (instance == nullptr || listener == nullptr) {
        return false;
    }

    instance_ = instance;
    listener_ = listener;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &OverlayWindow::WndProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;

    if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        platform::Log("RegisterClassEx 失败：%s", platform::LastErrorMessage(::GetLastError()).c_str());
        return false;
    }

    // 置顶 + 工具窗口（不进任务栏/Alt+Tab）+ 不可激活（不抢目标窗口焦点）。
    constexpr DWORD kExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    hwnd_ = ::CreateWindowExW(kExStyle, kClassName, L"Wind", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance_,
                              this);
    if (hwnd_ == nullptr) {
        platform::Log("CreateWindowEx 失败：%s", platform::LastErrorMessage(::GetLastError()).c_str());
        return false;
    }

    ::ShowWindow(hwnd_, SW_HIDE);
    return true;
}

void OverlayWindow::Destroy() {
    if (hwnd_ != nullptr) {
        ::SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (instance_ != nullptr) {
        ::UnregisterClassW(kClassName, instance_);
        instance_ = nullptr;
    }
    listener_ = nullptr;
}

void OverlayWindow::Show() {
    ApplyVirtualScreenBounds(true);
}

void OverlayWindow::Hide() {
    if (hwnd_ != nullptr) {
        ::ShowWindow(hwnd_, SW_HIDE);
    }
}

void OverlayWindow::ResizeToVirtualScreen() {
    ApplyVirtualScreenBounds(IsVisible());
}

void OverlayWindow::ApplyVirtualScreenBounds(bool show) {
    if (hwnd_ == nullptr) {
        return;
    }

    const RECT bounds = platform::VirtualScreenBounds();
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    if (show) {
        flags |= SWP_SHOWWINDOW;
    }
    ::SetWindowPos(hwnd_, HWND_TOPMOST, bounds.left, bounds.top, width, height, flags);

    if (show) {
        ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* self = reinterpret_cast<OverlayWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self != nullptr) {
        return self->HandleMessage(hwnd, message, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // 先交给 ImGui 更新内部输入状态；本层仍要继续处理鼠标消息，因此不使用其返回值。
    ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);

    switch (message) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            ::ValidateRect(hwnd, nullptr);
            return 0;

        case kWheelMessage:
            if (listener_ != nullptr) {
                listener_->OnWheel(static_cast<int>(static_cast<short>(LOWORD(wParam))));
            }
            return 0;

        case WM_MOUSEMOVE:
            if (listener_ != nullptr) {
                POINT cursor{};
                ::GetCursorPos(&cursor);
                listener_->OnPointerMoved(cursor);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (listener_ != nullptr) {
                listener_->OnPrimaryClick();
            }
            return 0;

        case WM_LBUTTONUP:
            return 0;

        case WM_RBUTTONDOWN:
            if (listener_ != nullptr) {
                listener_->OnDismissRequested();
            }
            return 0;

        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
            // 分辨率 / 缩放 / 显示器布局变化后重新贴合虚拟屏幕，否则描边会错位。
            ResizeToVirtualScreen();
            if (listener_ != nullptr) {
                listener_->OnDisplayChanged();
            }
            return 0;

        case WM_SETTINGCHANGE:
        case WM_SYSCOLORCHANGE:
            // 亮/暗主题切换会以 WM_SETTINGCHANGE(lParam = "ImmersiveColorSet") 广播，
            // 老系统上也可能只发 WM_SYSCOLORCHANGE；两者都转给应用层重读一次设置即可，
            // 读一个注册表 DWORD 的成本可以忽略，不必自己解析 lParam 字符串。
            if (listener_ != nullptr) {
                listener_->OnSystemThemeChanged();
            }
            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace wind::ui
