#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "wind/core/TargetWindow.h"
#include "wind/core/WindowProperties.h"
#include "wind/core/WindowRegistry.h"

namespace wind::core {

/// @brief 选取模式的领域状态。
///
/// 负责三件事：找到光标下的目标窗口、按窗口句柄缓存其属性、把界面发来的意图
/// 翻译成对 WindowProperties 的调用。这里没有任何 ImGui 代码，UI 只做“读数 + 发命令”。
///
/// 属性快照本身存放在外部的 WindowRegistry 里（由应用层持有），
/// 这样退出选取模式不会丢掉记录，程序退出时才能把调过的窗口全部还原。
class Selection {
public:
    /// @param overlayWindow 叠加层窗口句柄，命中测试时会跳过它。
    /// @param registry 属性快照簿，生命周期必须长于本对象。
    Selection(HWND overlayWindow, WindowRegistry& registry);

    /// @brief 更新指针位置并重新做命中测试。
    /// @param screenPoint 屏幕坐标（物理像素）。
    /// @return 目标是否发生变化，UI 可据此决定是否需要重绘。
    ///
    /// 指针下没有可选取的窗口时不会清空目标，而是保持上一次的目标（sticky），
    /// 否则鼠标一移出目标窗口（例如移向浮在窗口外侧的工具栏）选取就丢了。
    bool UpdatePointer(POINT screenPoint);

    bool HasTarget() const { return target_ != nullptr && target_->IsValid(); }

    /// @brief 目标窗口的可见边界；无目标时返回空矩形。
    RECT TargetBounds() const;
    std::wstring TargetTitle() const;
    DWORD TargetProcessId() const;

    int Opacity() const;
    bool AlwaysOnTop() const;
    bool OpacityLocked() const;

    /// @brief 相对调整透明度；@param delta 百分点，结果钳制在 kMinOpacity..100。
    void AdjustOpacity(int delta);

    /// @brief 直接把透明度设成指定百分点。
    void SetOpacity(int percent);

    /// @brief 切换置顶状态。
    void ToggleAlwaysOnTop();

    /// @brief 把当前目标恢复到进入选取模式时的属性。
    void Reset();

private:
    WindowProperties* Current();
    const WindowProperties* Lookup() const;

    HWND overlay_;
    WindowRegistry* registry_;
    std::unique_ptr<TargetWindow> target_;
};

}  // namespace wind::core
