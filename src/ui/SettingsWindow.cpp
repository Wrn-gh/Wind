#include "wind/ui/SettingsWindow.h"

#include <dwmapi.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <windowsx.h>

#include "wind/platform/Dpi.h"
#include "wind/platform/Win32Error.h"
#include "wind/ui/SettingsView.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace wind::ui {
namespace {

constexpr wchar_t kClassName[] = L"Wind.SettingsWindow";

// Windows 11 的圆角偏好；SDK 的枚举名在旧工具链里可能缺失，直接用数值更稳。
constexpr DWORD kDwmCornerPreference = 33;
constexpr int kCornerRound = 2;

}  // namespace

SettingsWindow::~SettingsWindow() {
    Destroy();
}

bool SettingsWindow::Create(HINSTANCE instance, Listener* listener) {
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
    // 无系统标题栏但仍需要投影，CS_DROPSHADOW 是唯一不引入 WS_CAPTION 的做法。
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    windowClass.lpfnWndProc = &SettingsWindow::WndProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;

    if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        platform::Log("RegisterClassEx 失败：%s", platform::LastErrorMessage(::GetLastError()).c_str());
        return false;
    }

    // 工具窗口（不进任务栏/Alt+Tab，与托盘程序一致）；不加 WS_EX_NOACTIVATE，
    // 设置页需要真实键盘焦点才能录制快捷键。
    hwnd_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"Wind 设置", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                             instance_, this);
    if (hwnd_ == nullptr) {
        platform::Log("CreateWindowEx 失败：%s", platform::LastErrorMessage(::GetLastError()).c_str());
        return false;
    }

    ::DwmSetWindowAttribute(hwnd_, kDwmCornerPreference, &kCornerRound, sizeof(kCornerRound));
    ::ShowWindow(hwnd_, SW_HIDE);
    return true;
}

void SettingsWindow::Destroy() {
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

void SettingsWindow::Show(float designWidth, float designHeight) {
    if (hwnd_ == nullptr) {
        return;
    }

    // 先确定要落在哪块屏幕上（鼠标所在），再按那块屏幕的 DPI 换算尺寸，
    // 否则多显示器混合缩放时窗口会跑到错误的屏幕或大小不对。
    POINT cursor{};
    ::GetCursorPos(&cursor);
    HMONITOR monitor = ::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);

    const float scale = platform::MonitorDpiScale(monitor);
    dpiScale_ = (scale > 0.0f) ? scale : 1.0f;

    const int width = static_cast<int>(designWidth * dpiScale_ + 0.5f);
    const int height = static_cast<int>(designHeight * dpiScale_ + 0.5f);

    const RECT work = platform::MonitorWorkArea(monitor);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;

    ::SetWindowPos(hwnd_, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::ShowWindow(hwnd_, SW_SHOWNORMAL);
    TopmostPulse();
    ::SetForegroundWindow(hwnd_);
}

void SettingsWindow::TopmostPulse() {
    // 程序常驻托盘、没有主窗口，直接 SetForegroundWindow 经常只闪一下任务栏；
    // 短暂置顶再取消置顶可以让窗口稳定出现在最前，但不长期压住用户其它窗口。
    ::SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ::SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void SettingsWindow::Hide() {
    if (hwnd_ != nullptr) {
        ::ShowWindow(hwnd_, SW_HIDE);
    }
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = reinterpret_cast<SettingsWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self != nullptr) {
        return self->HandleMessage(hwnd, message, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // 先交给 ImGui 更新内部输入状态；本层仍要继续处理按键消息，因此不使用其返回值。
    // 渲染器是懒初始化的：首次打开设置页之前没有 ImGui 上下文，此时跳过即可。
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
    }

    switch (message) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            ::ValidateRect(hwnd, nullptr);
            return 0;

        case WM_NCHITTEST: {
            // 顶部标题条整条可拖动窗口：交给系统 HTCAPTION 最省事，双击还原/拖动贴边
            // 都由系统实现。但落在 ImGui 控件上时必须返回 HTCLIENT，否则点不到按钮。
            const LRESULT hit = ::DefWindowProcW(hwnd, message, wParam, lParam);
            if (hit != HTCLIENT) {
                return hit;
            }
            POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ::ScreenToClient(hwnd, &cursor);
            const float titleBar = SettingsView::TitleBarHeight() * dpiScale_;
            const bool overItem = ImGui::GetCurrentContext() != nullptr && ImGui::IsAnyItemHovered();
            if (static_cast<float>(cursor.y) < titleBar && !overItem) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            // 录制快捷键时需要拿到原始按键（含 Esc 取消录制），所以先给页面处理。
            UINT modifiers = 0;
            if (::GetKeyState(VK_CONTROL) < 0) {
                modifiers |= MOD_CONTROL;
            }
            if (::GetKeyState(VK_MENU) < 0) {
                modifiers |= MOD_ALT;
            }
            if (::GetKeyState(VK_SHIFT) < 0) {
                modifiers |= MOD_SHIFT;
            }
            if (::GetKeyState(VK_LWIN) < 0 || ::GetKeyState(VK_RWIN) < 0) {
                modifiers |= MOD_WIN;
            }
            if (listener_ != nullptr && listener_->OnSettingsKeyDown(static_cast<UINT>(wParam), modifiers)) {
                return 0;
            }
            break;
        }

        case WM_SYSCHAR:
            // 无系统菜单：吞掉 Alt+字母 的提示音。
            return 0;

        case WM_DPICHANGED: {
            const float scale = static_cast<float>(HIWORD(wParam)) / 96.0f;
            dpiScale_ = (scale > 0.0f) ? scale : 1.0f;
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                           suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_CLOSE:
            // 关闭 = 取消；窗口只隐藏不销毁，程序继续驻留托盘。
            if (listener_ != nullptr) {
                listener_->OnSettingsCloseRequested();
            }
            return 0;

        case WM_DESTROY:
            // 刻意不调用 PostQuitMessage：设置页只是工具的一部分，销毁窗口不代表退出程序。
            return 0;

        default:
            break;
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace wind::ui
