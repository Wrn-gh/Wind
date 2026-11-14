#pragma once

#include <windows.h>

#include <string>

namespace wind::ui {

/// @brief 任务栏通知区图标：不记热键也能操作程序的入口。
///
/// 内部维护一条隐藏的消息窗口来接收 Shell 的图标回调，左键与右键都弹出同一个菜单，
/// 菜单项随当前的选取状态在“开始选取”与“结束选取”之间切换。
/// 图标优先取可执行文件同目录下的 wind.ico，取不到时用运行时生成的占位图标。
class TrayIcon {
public:
    /// @brief 用户从托盘菜单里发出的指令。
    enum class Command {
        None,
        ToggleSelection,
        Settings,
        Quit,
    };

    /// @brief 托盘事件回调，由应用层决定具体行为。
    class Listener {
    public:
        virtual ~Listener() = default;

        /// @brief 用户在托盘菜单里选择了一项。
        virtual void OnTrayCommand(Command command) = 0;
    };

    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    /// @brief 建立隐藏宿主窗口并注册通知区图标。
    /// @return 注册成功返回 true；失败时程序仍可运行，只是没有托盘入口。
    bool Create(HINSTANCE instance, Listener* listener);

    /// @brief 移除图标并销毁宿主窗口，可重复调用。
    void Destroy();

    /// @brief 同步选取状态，只影响菜单项文案。
    void SetSelecting(bool selecting) { selecting_ = selecting; }

    /// @brief 更新“开始选取”的快捷键文案（菜单项与悬浮提示都会跟着变）。
    /// @param text 形如 "Ctrl+Alt+W" 的文本。
    /// @param available false 表示当前没有生效的全局热键，菜单项不再显示快捷键。
    void SetToggleHotkey(std::wstring text, bool available);

    /// @brief 弹出一条托盘气泡提示（非模态、不抢焦点）。
    /// @param title 标题，最多 63 个字符。
    /// @param text 正文，最多 255 个字符。
    /// @param warning true 用警告图标，false 用信息图标。
    void Notify(const wchar_t* title, const wchar_t* text, bool warning);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void AddIcon();
    void RemoveIcon();
    void UpdateTip();
    void ShowMenu(POINT anchor);

    static constexpr UINT kCallbackMessage = WM_APP + 2;
    static constexpr UINT kIconId = 1;
    static constexpr UINT kMenuToggle = 1;
    static constexpr UINT kMenuSettings = 2;
    static constexpr UINT kMenuQuit = 3;

    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    Listener* listener_ = nullptr;
    bool selecting_ = false;
    bool added_ = false;
    UINT taskbarCreatedMessage_ = 0;
    std::wstring toggleHotkeyText_ = L"Ctrl+Alt+W";
    bool hotkeyAvailable_ = true;
};

}  // namespace wind::ui
