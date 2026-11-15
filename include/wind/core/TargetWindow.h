#pragma once

#include <windows.h>

#include <string>
#include <vector>

/// @brief 领域层：只依赖平台层，不含任何 ImGui / 绘制代码，可脱离界面独立验证。
namespace wind::core {

class IgnoreList;

/// @brief 目标窗口的轻量引用。
///
/// 只保存 HWND，所有查询都在调用时重新执行，因此不会出现“状态过期”的问题；
/// 反过来调用方也不能缓存返回值，窗口可能随时被关闭。
class TargetWindow {
public:
    TargetWindow() = default;
    explicit TargetWindow(HWND hwnd) : hwnd_(hwnd) {}

    /// @brief 按 Z 序从顶到底查找屏幕某点处的用户窗口。
    /// @param screenPoint 屏幕坐标（物理像素）。
    /// @param exclude 需要跳过的窗口，通常是自己的叠加层窗口。
    /// @param ignore 用户维护的忽略规则；命中规则的窗口被整个跳过，nullptr 表示不忽略。
    /// @return 命中的窗口；没有命中时返回空对象（Hwnd() == nullptr）。
    static TargetWindow FindAt(POINT screenPoint, HWND exclude, const IgnoreList* ignore = nullptr);

    HWND Hwnd() const { return hwnd_; }
    bool IsValid() const { return hwnd_ != nullptr && ::IsWindow(hwnd_) != FALSE; }

    /// @brief 窗口的可见边界（物理像素）。
    /// 优先使用 DWMWA_EXTENDED_FRAME_BOUNDS，去掉 Win10+ 窗口阴影占用的不可见边距。
    RECT Bounds() const;

    /// @brief 窗口标题；无标题窗口返回空串。
    std::wstring Title() const;

    /// @brief 窗口类名；句柄失效时返回空串。
    std::wstring ClassName() const;

    /// @brief 窗口所属进程 ID；句柄失效时返回 0。
    DWORD ProcessId() const;

private:
    HWND hwnd_ = nullptr;
};

/// @brief 内置硬编码排除的外壳窗口类名（桌面、任务栏等）。
///
/// 这是与用户规则无关的最后防线：即使忽略列表被清空，这些窗口也永远不会被选中。
/// 设置页会把它列出来，解释“为什么有些窗口根本点不到”。
const std::vector<std::wstring>& BuiltinExcludedClasses();

}  // namespace wind::core
