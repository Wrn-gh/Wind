#pragma once

#include <windows.h>

namespace wind::platform {

/// @brief 全局热键的 RAII 封装：析构时自动注销，避免进程内残留注册。
class Hotkey {
public:
    Hotkey() = default;
    ~Hotkey();

    Hotkey(const Hotkey&) = delete;
    Hotkey& operator=(const Hotkey&) = delete;

    /// @brief 注册一个全局热键；重复注册会先注销旧的注册。
    /// @param owner 接收 WM_HOTKEY 的窗口；传 nullptr 表示投递到线程消息队列（hwnd 为 0）。
    /// @param id 热键标识，需在进程内唯一。
    /// @param modifiers MOD_* 组合（建议带 MOD_NOREPEAT，避免长按连发）。
    /// @param virtualKey 虚拟键码，例如 'W' 或 VK_ESCAPE。
    /// @return 注册成功返回 true；失败返回 false 并记录原因（常见原因是被其他程序占用）。
    bool Register(HWND owner, int id, UINT modifiers, UINT virtualKey);

    /// @brief 注销当前注册；未注册时为空操作。
    void Unregister();

    /// @brief 判断 WM_HOTKEY 的 wParam 是否来自本对象。
    bool Matches(WPARAM hotkeyId) const { return registered_ && static_cast<int>(hotkeyId) == id_; }

    bool IsRegistered() const { return registered_; }

private:
    HWND owner_ = nullptr;
    int id_ = 0;
    bool registered_ = false;
};

}  // namespace wind::platform
