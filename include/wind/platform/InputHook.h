#pragma once

#include <windows.h>

namespace wind::platform {

/// @brief 选取模式下的滚轮接管。
///
/// 滚轮消息（WM_MOUSEWHEEL）只会投递给焦点窗口，而叠加层刻意不抢焦点，
/// 因此这里用 WH_MOUSE_LL 低级钩子把滚轮增量转发给叠加层，并吞掉原始消息，
/// 避免“调透明度的同时把目标窗口的内容也滚动了”。
class WheelHook {
public:
    WheelHook() = default;
    ~WheelHook();

    WheelHook(const WheelHook&) = delete;
    WheelHook& operator=(const WheelHook&) = delete;

    /// @brief 安装钩子。
    /// @param sink 接收消息的窗口。
    /// @param sinkMessage 自定义消息号，wParam 保存滚轮增量（有符号，向上为正）。
    /// @return 安装成功返回 true。低级钩子回调是静态函数，全进程同时只能有一个实例。
    bool Install(HWND sink, UINT sinkMessage);

    /// @brief 卸载钩子；未安装时为空操作。
    void Uninstall();

    bool IsInstalled() const { return hook_ != nullptr; }

private:
    static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam);

    static WheelHook* active_;

    HWND sink_ = nullptr;
    UINT sinkMessage_ = 0;
    HHOOK hook_ = nullptr;
};

}  // namespace wind::platform
