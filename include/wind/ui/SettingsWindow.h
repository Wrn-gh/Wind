#pragma once

#include <windows.h>

namespace wind::ui {

/// @brief 设置页宿主窗口：无系统标题栏、不进任务栏、不抢焦点层级的最小外壳。
///
/// 只负责窗口创建、样式、拖动与消息转发；页面内容与交互全部由 SettingsView 绘制。
/// 与 OverlayWindow 的两处关键差异：
///  - 不允许 WM_DESTROY 里 PostQuitMessage，关闭设置页只是隐藏，程序必须继续驻留托盘；
///  - 不使用 WS_EX_NOACTIVATE，设置页需要正常接收键盘输入（录制快捷键）。
class SettingsWindow {
public:
    /// @brief 设置页向外抛出的事件。
    class Listener {
    public:
        virtual ~Listener() = default;

        /// @brief 用户点关闭按钮或按 Esc / Alt+F4（等同「取消」）。
        virtual void OnSettingsCloseRequested() = 0;
        /// @brief 设置页内按下按键，用于录制快捷键。
        /// @param virtualKey 虚拟键码。
        /// @param modifiers 当前按下的修饰键（MOD_* 位组合）。
        /// @return 已消费该按键返回 true，否则交给默认处理（例如 Tab 切换焦点）。
        virtual bool OnSettingsKeyDown(UINT virtualKey, UINT modifiers) = 0;
    };

    SettingsWindow() = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    /// @brief 创建窗口（保持隐藏状态，等待 Show）。
    bool Create(HINSTANCE instance, Listener* listener);

    void Destroy();

    /// @brief 按 96 DPI 设计尺寸乘以当前 DPI 缩放，居中于鼠标所在显示器的窗口区后显示。
    /// @param designWidth 96 DPI 下的宽度。
    /// @param designHeight 96 DPI 下的高度。
    void Show(float designWidth, float designHeight);

    /// @brief 隐藏设置页（不销毁，下次打开无需重建 D3D 设备）。
    void Hide();

    HWND Hwnd() const { return hwnd_; }
    bool IsVisible() const { return hwnd_ != nullptr && ::IsWindowVisible(hwnd_) != FALSE; }
    /// @brief 当前 DPI 缩放（96 DPI 对应 1.0），随 WM_DPICHANGED 更新。
    float DpiScale() const { return dpiScale_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void TopmostPulse();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    Listener* listener_ = nullptr;
    float dpiScale_ = 1.0f;
};

}  // namespace wind::ui
