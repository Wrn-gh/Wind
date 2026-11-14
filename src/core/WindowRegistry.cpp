#include "wind/core/WindowRegistry.h"

#include <iterator>

#include "wind/platform/Win32Error.h"

namespace wind::core {

WindowProperties& WindowRegistry::Acquire(HWND hwnd) {
    return entries_.try_emplace(hwnd, hwnd).first->second;
}

const WindowProperties* WindowRegistry::Find(HWND hwnd) const {
    const auto it = entries_.find(hwnd);
    return it == entries_.end() ? nullptr : &it->second;
}

size_t WindowRegistry::RestoreAll() {
    size_t restored = 0;
    for (auto& [hwnd, properties] : entries_) {
        if (!properties.IsValid() || !properties.Modified()) {
            continue;
        }
        // Restore 会把置顶/透明度写回快照值，之后再调用 Modified() 即为 false，
        // 因此重复调用 RestoreAll 不会对同一个窗口反复折腾。
        properties.Restore();
        ++restored;
        platform::Log("退出还原：hwnd=0x%p 已恢复原始透明度/置顶状态。", static_cast<void*>(hwnd));
    }
    return restored;
}

void WindowRegistry::DropDeadWindows() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        it = (::IsWindow(it->first) == FALSE) ? entries_.erase(it) : std::next(it);
    }
}

}  // namespace wind::core
