#pragma once

#include <windows.h>

namespace wind::core {

/// @brief 目标窗口的「置顶 / 透明度」属性读写，并持有进入选取模式时的原始状态快照。
///
/// 实现要点：
/// - 透明度通过临时追加 WS_EX_LAYERED + SetLayeredWindowAttributes(LWA_ALPHA) 实现，
///   Win8 之后允许跨进程调用，不需要注入目标进程。
/// - 目标若以更高权限运行，UIPI 会拒绝调用，此时 OpacityLocked() 返回 true，
///   界面应据此置灰并提示，而不是静默失败。
/// - 快照保证「重置」能回到进入时的状态，而不是粗暴地恢复成 100%。
class WindowProperties {
public:
    /// 透明度下限：低于 10% 目标几乎不可见，用户会失去重新选中它的能力。
    static constexpr int kMinOpacity = 10;

    explicit WindowProperties(HWND hwnd);

    WindowProperties(const WindowProperties&) = delete;
    WindowProperties& operator=(const WindowProperties&) = delete;
    WindowProperties(WindowProperties&&) = default;
    WindowProperties& operator=(WindowProperties&&) = default;

    bool IsValid() const { return hwnd_ != nullptr && ::IsWindow(hwnd_) != FALSE; }

    /// @brief 当前透明度（百分比，10..100）。
    int Opacity() const { return opacity_; }

    /// @brief 当前是否置顶。
    bool AlwaysOnTop() const { return topmost_; }

    /// @brief 目标是否拒绝透明度修改（典型原因：目标以管理员身份运行）。
    bool OpacityLocked() const { return opacityLocked_; }

    /// @brief 设置透明度；值会被钳制在 kMinOpacity..100。
    /// @return 实际发生变化返回 true；目标失效、被拒绝或值未变返回 false。
    bool SetOpacity(int percent);

    /// @brief 设置置顶状态。
    /// @return 实际发生变化返回 true。
    bool SetAlwaysOnTop(bool enabled);

    /// @brief 恢复到构造时的原始状态（置顶 + 透明度 + 是否分层窗口）。
    void Restore();

    /// @brief 当前状态是否已经偏离构造时的快照（需要 Restore 才算干净）。
    ///
    /// 「退出还原」只处理真正被改过的窗口，避免对用户根本没碰过的窗口
    /// 也调用一遍 SetWindowPos，白白让窗口闪烁一次。
    bool Modified() const;

private:
    bool ApplyAlpha(unsigned char alpha);
    bool RestoreLayeredState();
    /// @brief 快照里的透明度百分比（原本不是分层窗口时为 100）。
    int OriginalOpacity() const;

    HWND hwnd_ = nullptr;

    int opacity_ = 100;
    bool topmost_ = false;
    bool opacityLocked_ = false;

    // 进入选取模式时的快照
    LONG_PTR exStyle_ = 0;
    bool originalTopmost_ = false;
    bool originalLayered_ = false;
    bool hasOriginalAlpha_ = false;
    unsigned char originalAlpha_ = 255;
};

}  // namespace wind::core
