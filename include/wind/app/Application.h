#pragma once

#include <windows.h>

#include <memory>

#include "wind/core/IgnoreList.h"
#include "wind/core/Settings.h"
#include "wind/core/Selection.h"
#include "wind/core/TargetWindow.h"
#include "wind/core/WindowRegistry.h"
#include "wind/platform/Hotkey.h"
#include "wind/platform/InputHook.h"
#include "wind/platform/Renderer.h"
#include "wind/platform/SingleInstance.h"
#include "wind/ui/IconSet.h"
#include "wind/ui/IgnorePickerView.h"
#include "wind/ui/OverlayWindow.h"
#include "wind/ui/SettingsView.h"
#include "wind/ui/SettingsWindow.h"
#include "wind/ui/Theme.h"
#include "wind/ui/ToolbarView.h"
#include "wind/ui/TrayIcon.h"

namespace wind::app {

/// @brief 启动时的热键注册结果，供应用层决定要不要弹托盘提示。
enum class HotkeyStatus {
    Ok,
    /// @brief 至少有一组配置的键被占用；occupancy 细节由两个 bool 成员记录。
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
    /// @brief 进入「选取要忽略的窗口」模式；与选取态、设置页互斥。
    void EnterIgnorePicker();
    /// @brief 退出忽略选取模式并隐藏叠加层。
    void LeaveIgnorePicker();
    /// @brief 把光标准星处的窗口加入忽略列表并退出该模式。
    void CommitIgnoreTarget();
    /// @brief 按指针位置刷新忽略模式的准星目标。
    void UpdateIgnoreTarget(POINT screenPoint);
    /// @brief 忽略选取模式的一帧：只描边，不画工具栏。
    void RenderIgnorePickerFrame();
    void RenderFrame();
    void ApplyCommand(const ui::ToolbarView::Command& command);
    /// @brief 按叠加层所在显示器的 DPI 同步渲染器与工具栏的界面缩放。
    void SyncUiScale();
    /// @brief 按已保存的设置重算配色并同步到全部渲染器。
    void SyncTheme();
    /// @brief 把设置里的语言解析成具体界面语言，有变化时广播给各个视图。
    void ApplyUiLanguage(const core::Settings& settings);
    /// @brief 用给定设置做实时预览（设置页每帧调用），不落盘。
    void PreviewTheme(const core::Settings& settings);
    /// @brief 读取设置文件并注册热键；任一组被占用时对应 available 标记为 false。
    HotkeyStatus LoadHotkey();
    /// @brief 注册一个具体的组合键（内部会先注销旧注册）。
    bool RegisterHotkey(platform::Hotkey& hotkey, int id, const core::HotkeyBinding& binding);
    /// @brief 把一组热键换成新值；失败时立刻恢复旧键，返回 false。
    bool ApplyHotkey(platform::Hotkey& hotkey, int id, const core::HotkeyBinding& binding,
                     core::HotkeyBinding& active, bool& available);
    /// @brief 应用新的选取热键。
    bool ApplyToggleHotkey(const core::HotkeyBinding& binding) {
        return ApplyHotkey(toggleHotkey_, kToggleHotkeyId, binding, activeHotkey_, toggleHotkeyAvailable_);
    }
    /// @brief 应用新的忽略选取热键。
    bool ApplyIgnoreHotkey(const core::HotkeyBinding& binding) {
        return ApplyHotkey(ignoreHotkey_, kIgnoreHotkeyId, binding, activeIgnoreHotkey_, ignoreHotkeyAvailable_);
    }
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
    static constexpr int kIgnoreHotkeyId = 3;
    static constexpr UINT_PTR kFrameTimerId = 1;
    static constexpr UINT_PTR kSettingsTimerId = 2;
    static constexpr UINT kFrameIntervalMs = 16;
    /// 设置页在 96 DPI 下的设计尺寸。
    static constexpr float kSettingsWidth = 600.0f;
    static constexpr float kSettingsHeight = 620.0f;

    HINSTANCE instance_ = nullptr;
    /// 单实例守卫：互斥体必须和进程同寿，所以声明在最靠前，析构时自动放开。
    platform::SingleInstance singleInstance_;
    ui::OverlayWindow overlay_;
    platform::Renderer renderer_;
    platform::Hotkey toggleHotkey_;
    platform::Hotkey ignoreHotkey_;
    platform::Hotkey escapeHotkey_;
    platform::WheelHook wheelHook_;
    ui::ToolbarView toolbar_;
    /// 工具栏图标纹理；必须在 renderer_.Shutdown() 之前释放，所以声明在它之后。
    ui::IconSet icons_;
    ui::IgnorePickerView ignorePickerView_;
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
    /// 用户维护的忽略规则；Selection 每帧命中测试都会查它。
    core::IgnoreList ignoreList_;
    /// 当前实际注册成功的选取热键；没有可用热键时仍保留配置值，仅注册状态为 false。
    core::HotkeyBinding activeHotkey_ = core::kDefaultSettings.toggleHotkey;
    /// 当前实际注册成功的忽略选取热键，语义同上。
    core::HotkeyBinding activeIgnoreHotkey_ = core::kDefaultSettings.ignoreHotkey;
    /// 忽略选取模式下光标所指的窗口（点一下才会真正加入规则）。
    core::TargetWindow ignoreTarget_;
    ui::Theme theme_ = ui::kDarkTheme;
    /// 已生效的界面语言；settings_.language 可能是 System，这里永远是具体语言。
    core::Language uiLanguage_ = core::Language::System;
    bool selecting_ = false;
    /// 忽略选取模式：叠加层可见，但只描边、不画工具栏。
    bool ignorePicking_ = false;
    bool lightTheme_ = false;
    bool settingsWindowReady_ = false;
    bool settingsRendererReady_ = false;
    bool settingsOpen_ = false;
    /// 选取热键当前是否可用；false 表示配置键被占用。
    bool toggleHotkeyAvailable_ = false;
    /// 忽略选取热键当前是否可用。
    bool ignoreHotkeyAvailable_ = false;
};

}  // namespace wind::app
