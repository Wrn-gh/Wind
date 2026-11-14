#pragma once

#include <windows.h>

#include <memory>

#include "wind/core/Settings.h"
#include "wind/core/Selection.h"
#include "wind/core/WindowRegistry.h"
#include "wind/platform/Hotkey.h"
#include "wind/platform/InputHook.h"
#include "wind/platform/Renderer.h"
#include "wind/platform/SingleInstance.h"
#include "wind/ui/IconSet.h"
#include "wind/ui/OverlayWindow.h"
#include "wind/ui/SettingsView.h"
#include "wind/ui/SettingsWindow.h"
#include "wind/ui/Theme.h"
#include "wind/ui/ToolbarView.h"
#include "wind/ui/TrayIcon.h"

namespace wind::app {

/// @brief 启动时选取热键的最终结果，供应用层决定要不要弹托盘提示。
enum class HotkeyStatus {
    Ok,
    /// @brief 配置的键被占用，本次启动没有全局热键，只留托盘入口。
    Unavailable,
};

/// @brief 组装层：唯一知道全部模块的地方，负责生命周期、消息循环与模式切换。
///
/// 状态机只有三个状态：
///   Idle      —— 叠加层与设置页都隐藏，GetMessage 阻塞等待，CPU 占用 ≈ 0；
///   Selecting —— 叠加层可见且置顶，16 ms 定时器驱动重绘，模态接收鼠标；
///   Settings  —— 设置页可见，16 ms 定时器驱动重绘，实时预览配色。
/// 切换入口：全局热键、托盘菜单。设置页与选取态互斥，避免两个 ImGui 上下文抢输入。
class Application final : public ui::OverlayWindow::Listener,
                          public ui::TrayIcon::Listener,
                          public ui::SettingsWindow::Listener {
public:
    Application() = default;
    ~Application() override = default;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// @brief 启动并运行消息循环，直到收到 WM_QUIT。
    /// @return 进程退出码。
    int Run(HINSTANCE instance);

private:
    // ui::OverlayWindow::Listener
    void OnPointerMoved(POINT screenPoint) override;
    void OnWheel(int delta) override;
    void OnPrimaryClick() override;
    void OnDismissRequested() override;
    void OnDisplayChanged() override;
    void OnSystemThemeChanged() override;

    // ui::TrayIcon::Listener
    void OnTrayCommand(ui::TrayIcon::Command command) override;

    // ui::SettingsWindow::Listener
    void OnSettingsCloseRequested() override;
    bool OnSettingsKeyDown(UINT virtualKey, UINT modifiers) override;

    void HandleHotkey(int hotkeyId);
    void EnterSelection();
    void LeaveSelection();
    void RenderFrame();
    void ApplyCommand(const ui::ToolbarView::Command& command);
    /// @brief 按叠加层所在显示器的 DPI 同步渲染器与工具栏的界面缩放。
    void SyncUiScale();
    /// @brief 按已保存的设置重算配色并同步到全部渲染器。
    void SyncTheme();
    /// @brief 用给定设置做实时预览（设置页每帧调用），不落盘。
    void PreviewTheme(const core::Settings& settings);
    /// @brief 读取设置文件并注册热键；被占用时不注册全局热键，返回 Unavailable。
    HotkeyStatus LoadSettingsAndHotkey();
    /// @brief 注册一个具体的组合键（内部会先注销旧注册）。
    bool RegisterToggleHotkey(const core::HotkeyBinding& binding);
    /// @brief 应用新的选取热键；失败时立刻恢复旧键，返回 false。
    bool ApplyToggleHotkey(const core::HotkeyBinding& binding);
    /// @brief 让托盘文案与当前生效的热键保持一致。
    void SyncTrayHotkey();
    /// @brief 启动时校验自启项：路径变了就覆写，项被外部删掉了就把配置同步回来。
    void SyncAutoStart();
    /// @brief 打开设置页（必要时懒初始化它的渲染器）。
    void OpenSettings();
    /// @brief 关闭设置页；confirmed 为 true 表示已落盘，否则丢弃预览改动。
    void CloseSettings(bool confirmed);
    /// @brief 设置页的一帧：实时预览 + 处理确定/取消。
    void RenderSettingsFrame();
    /// @brief 把本进程改动过的窗口全部还原（退出/系统注销时调用）。
    void RestoreAllWindows();

    static constexpr int kToggleHotkeyId = 1;
    static constexpr int kEscapeHotkeyId = 2;
    static constexpr UINT_PTR kFrameTimerId = 1;
    static constexpr UINT_PTR kSettingsTimerId = 2;
    static constexpr UINT kFrameIntervalMs = 16;
    /// 设置页在 96 DPI 下的设计尺寸。
    static constexpr float kSettingsWidth = 560.0f;
    static constexpr float kSettingsHeight = 420.0f;

    HINSTANCE instance_ = nullptr;
    /// 单实例守卫：互斥体必须和进程同寿，所以声明在最靠前，析构时自动放开。
    platform::SingleInstance singleInstance_;
    ui::OverlayWindow overlay_;
    platform::Renderer renderer_;
    platform::Hotkey toggleHotkey_;
    platform::Hotkey escapeHotkey_;
    platform::WheelHook wheelHook_;
    ui::ToolbarView toolbar_;
    /// 工具栏图标纹理；必须在 renderer_.Shutdown() 之前释放，所以声明在它之后。
    ui::IconSet icons_;
    ui::TrayIcon tray_;

    /// 设置页窗口与它独立的渲染器/ImGui 上下文。
    ui::SettingsWindow settingsWindow_;
    platform::Renderer settingsRenderer_;
    ui::SettingsView settingsView_;
    /// 设置页的图标纹理（属于 settingsRenderer_ 的设备，不能借用工具栏那份），
    /// 同样必须在 settingsRenderer_.Shutdown() 之前释放，故声明在它之后。
    ui::IconSet settingsIcons_;

    /// 属性快照簿必须活到进程结束：退出时靠它把所有调过的窗口还原回去。
    core::WindowRegistry registry_;
    std::unique_ptr<core::Selection> selection_;
    core::Settings settings_ = core::kDefaultSettings;
    /// 当前实际注册成功的选取热键；没有可用热键时仍保留配置值，仅注册状态为 false。
    core::HotkeyBinding activeHotkey_ = core::kDefaultSettings.toggleHotkey;
    ui::Theme theme_ = ui::kDarkTheme;
    bool selecting_ = false;
    bool lightTheme_ = false;
    bool settingsWindowReady_ = false;
    bool settingsRendererReady_ = false;
    bool settingsOpen_ = false;
    /// 选取热键当前是否可用；false 表示配置键与默认键都被占用。
    bool toggleHotkeyAvailable_ = false;
};

}  // namespace wind::app
