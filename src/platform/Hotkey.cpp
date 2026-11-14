#include "wind/platform/Hotkey.h"

#include "wind/platform/Win32Error.h"

namespace wind::platform {

Hotkey::~Hotkey() {
    Unregister();
}

bool Hotkey::Register(HWND owner, int id, UINT modifiers, UINT virtualKey) {
    Unregister();

    if (::RegisterHotKey(owner, id, modifiers, virtualKey) == FALSE) {
        Log("注册热键失败（id=%d, vk=0x%02X）：%s", id, virtualKey, LastErrorMessage(::GetLastError()).c_str());
        return false;
    }

    owner_ = owner;
    id_ = id;
    registered_ = true;
    return true;
}

void Hotkey::Unregister() {
    if (!registered_) {
        return;
    }
    ::UnregisterHotKey(owner_, id_);
    registered_ = false;
    owner_ = nullptr;
}

}  // namespace wind::platform
