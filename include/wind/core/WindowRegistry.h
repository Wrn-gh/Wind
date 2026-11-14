#pragma once

#include <windows.h>

#include <cstddef>
#include <unordered_map>

#include "wind/core/WindowProperties.h"

namespace wind::core {

/// @brief 进程内所有被改动过的窗口的属性簿。
///
/// 之所以独立于 Selection：选取模式可以反复进入/退出，而「退出程序时还原所有窗口」
/// 必须跨多次选取会话记住每一条原始快照。若快照跟着 Selection 一起销毁，
/// 用户只要进出过一次选取模式，之前调过的窗口就再也还原不回去了。
class WindowRegistry {
public:
    WindowRegistry() = default;
    WindowRegistry(const WindowRegistry&) = delete;
    WindowRegistry& operator=(const WindowRegistry&) = delete;

    /// @brief 取窗口的属性对象，首次访问时建立原始状态快照。
    /// @return 引用在 map 扩容时保持有效（unordered_map 的节点地址稳定）。
    WindowProperties& Acquire(HWND hwnd);

    /// @brief 查找已有快照；该窗口从未被改动过则返回 nullptr。
    const WindowProperties* Find(HWND hwnd) const;

    /// @brief 还原所有仍存活、且确实被改动过的窗口。
    /// @return 实际执行还原的窗口数量。
    size_t RestoreAll();

    /// @brief 丢弃已销毁窗口的快照（句柄可能被复用，留着会张冠李戴）。
    void DropDeadWindows();

    /// @brief 记录中的窗口总数（含未被改动的），用于日志。
    size_t Size() const { return entries_.size(); }

private:
    std::unordered_map<HWND, WindowProperties> entries_;
};

}  // namespace wind::core
