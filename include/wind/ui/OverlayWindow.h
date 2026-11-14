#pragma once

#include <windows.h>

#include <functional>

namespace wind::ui {

/// @brief 覆盖整个虚拟屏幕的逐像素透明叠加窗口。
///
/// 只负责窗口创建、样式与消息转发，不含任何业务判断；业务响应由 Listener 决定。
/// 样式关键点：WS_EX_NOACTIVATE + WM_MOUSEACTIVATE 返回 MA_NOACTIVATE，
/// 保证选取模式不会把焦点从目标窗口抢走（目标窗口不会变成灰色标题栏）。
class OverlayWindow {
public:
    /// @brief 叠加层向外抛出的事件。
    class Listener {
    public:
        virtual ~Listener() = default;

        /// @brief 鼠标在叠加层内移动。
        virtual void OnPointerMoved(POINT screenPoint) = 0;
        /// @brief 滚轮增量（向上为正），由低级鼠标钩子转发。
        virtual void OnWheel(int delta) = 0;
        /// @brief 左键按下（是否落在工具栏上由实现方结合 ImGui 判断）。
        virtual void OnPrimaryClick() = 0;
        /// @brief 用户请求退出选取模式（右键）。
        virtual void OnDismissRequested() = 0;
        /// @brief 显示器布局变化，渲染目标尺寸需要重建。
        virtual void OnDisplayChanged() = 0;
        /// @brief 系统颜色/主题设置变化（亮色 ↔ 暗色切换），配色需要重新读取。
        virtual void OnSystemThemeChanged() = 0;
    };

    /// @brief 滚轮增量消息号，见 platform::WheelHook。
    static constexpr UINT kWheelMessage = WM_APP + 1;

    OverlayWindow() = default;
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    /// @brief 创建窗口（保持隐藏状态，等待 Show）。
    bool Create(HINSTANCE instance, Listener* listener);

    void Destroy();

    /// @brief 覆盖虚拟屏幕并显示，同时置于最顶层。
    void Show();

    /// @brief 隐藏叠加层（不销毁）。
    void Hide();

    /// @brief 显示器布局变化后重新贴合虚拟屏幕。
    void ResizeToVirtualScreen();

    HWND Hwnd() const { return hwnd_; }
    bool IsVisible() const { return hwnd_ != nullptr && ::IsWindowVisible(hwnd_) != FALSE; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void ApplyVirtualScreenBounds(bool show);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    Listener* listener_ = nullptr;
};

}  // namespace wind::ui
