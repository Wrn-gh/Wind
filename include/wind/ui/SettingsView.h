#pragma once

#include <windows.h>

#include <string>

#include "wind/core/Settings.h"
#include "wind/ui/Theme.h"

namespace wind::ui {

class IconSet;

/// @brief 设置页的页面内容与交互。
///
/// 与 ToolbarView 一致：只持有“工作副本”，不写文件、不注册热键。应用层每帧读取
/// Working() 做实时预览，「确定」时才落盘，「取消」时把打开前的副本写回即可回滚。
class SettingsView {
public:
    /// @brief 页面返回的意图。
    enum class Action { None, Confirm, Cancel };

    /// @brief 打开页面并准备一份可编辑的工作副本。
    /// @param settings 已保存的设置（「取消」时恢复成它）。
    /// @param activeBinding 当前实际生效的热键；若热键整体不可用，这里是没有意义的占位值。
    /// @param hotkeyAvailable false 表示启动时热键被占用，当前没有任何全局热键。
    void Open(const core::Settings& settings, const core::Settings& activeBinding, bool hotkeyAvailable);

    /// @brief 绘制一帧。
    Action Draw();

    /// @brief 处理按键（录制快捷键）。由设置页窗口在 WM_KEYDOWN / WM_SYSKEYDOWN 转发。
    /// @param virtualKey 虚拟键码。
    /// @param modifiers 当前按下的修饰键（MOD_* 位组合），由窗口层读取键盘状态后传入。
    /// @return 已消费该按键返回 true。
    bool OnKeyDown(UINT virtualKey, UINT modifiers);

    /// @brief 显示一行提示（例如热键已降级），传空字符串清除。
    void SetNotice(const char* notice);
    /// @brief 显示一行错误（例如新热键被占用），传空字符串清除。
    void SetError(const char* error);

    /// @brief 用户正在编辑的设置副本，供应用层做实时预览。
    const core::Settings& Working() const { return working_; }
    /// @brief 是否正在录制新的快捷键。
    bool Recording() const { return recording_; }

    void SetTheme(const Theme& theme) { theme_ = theme; }
    void SetUiScale(float scale) { uiScale_ = (scale > 0.0f) ? scale : 1.0f; }

    /// @brief 绑定图标纹理集合；必须在设置页自己的渲染器初始化后调用。
    /// @note 图标纹理属于创建它的 D3D11 设备，不能借用工具栏那一份，故由调用方传入。
    ///       传 nullptr 或图标缺失时，标题条的关闭按钮回落到文字「x」。
    void SetIcons(const IconSet* icons) { icons_ = icons; }

    /// @brief 标题条在 96 DPI 下的高度，窗口层用它实现拖动命中测试。
    static float TitleBarHeight();

private:
    /// @brief 把 96 DPI 下的设计尺寸换算成当前 DPI 的物理像素。
    float Px(float value) const { return value * uiScale_; }

    void DrawTitleBar(bool& requestClose);
    /// @brief 绘制「常规」区块，返回下一区块的顶端 Y（物理像素）。
    float DrawGeneralSection(float left, float top);
    /// @brief 绘制「外观」区块，返回底部 Y（物理像素）。
    float DrawAppearanceSection(float left, float top);
    Action DrawFooter(float width, float height);
    void DrawSectionTitle(const char* text, float left, float top);
    void DrawLabel(const char* text, float left, float top);

    bool DrawTextButton(const char* id, const char* label, float x, float y, float width, bool selected,
                        bool primary);
    /// @brief 自绘胶囊开关；点击后翻转 value 并返回 true。
    bool DrawSwitch(const char* id, float x, float y, bool& value);
    /// @brief 主题三选一的分段按钮行。
    void DrawThemeSegments(float x, float y);

    core::Settings working_ = core::kDefaultSettings;
    core::Settings opened_ = core::kDefaultSettings;
    core::Settings active_ = core::kDefaultSettings;
    bool hotkeyAvailable_ = true;
    bool recording_ = false;
    bool cancelRequested_ = false;
    /// @brief 主题色取色器的浮点缓存，避免每帧从 0xRRGGBB 反推来回抖动。
    float accentColor_[3] = {0.0f, 0.588f, 0.941f};
    std::string notice_;
    std::string error_;
    Theme theme_ = kDarkTheme;
    float uiScale_ = 1.0f;
    const IconSet* icons_ = nullptr;
};

}  // namespace wind::ui
