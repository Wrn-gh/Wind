#include "wind/platform/InputHook.h"

#include "wind/platform/Win32Error.h"

namespace wind::platform {

WheelHook* WheelHook::active_ = nullptr;

WheelHook::~WheelHook() {
    Uninstall();
}

bool WheelHook::Install(HWND sink, UINT sinkMessage) {
    Uninstall();

    if (active_ != nullptr) {
        Log("滚轮钩子安装失败：已有实例在使用中");
        return false;
    }

    sink_ = sink;
    sinkMessage_ = sinkMessage;
    // 低级鼠标钩子在本进程上下文执行，hMod 传当前模块句柄即可。
    hook_ = ::SetWindowsHookExW(WH_MOUSE_LL, &WheelHook::HookProc, ::GetModuleHandleW(nullptr), 0);
    if (hook_ == nullptr) {
        Log("SetWindowsHookEx(WH_MOUSE_LL) 失败：%s", LastErrorMessage(::GetLastError()).c_str());
        sink_ = nullptr;
        sinkMessage_ = 0;
        return false;
    }

    active_ = this;
    return true;
}

void WheelHook::Uninstall() {
    if (hook_ != nullptr) {
        ::UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    if (active_ == this) {
        active_ = nullptr;
    }
    sink_ = nullptr;
    sinkMessage_ = 0;
}

LRESULT CALLBACK WheelHook::HookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == WM_MOUSEWHEEL) {
        WheelHook* self = active_;
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        if (self != nullptr && info != nullptr && (info->flags & LLMHF_INJECTED) == 0) {
            // 钩子回调必须尽快返回，真正的处理通过消息投递到界面线程。
            const int delta = static_cast<int>(static_cast<short>(HIWORD(info->mouseData)));
            ::PostMessageW(self->sink_, self->sinkMessage_, static_cast<WPARAM>(delta), 0);
            return 1;  // 吞掉原始消息，避免目标窗口跟着滚动
        }
    }
    return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

}  // namespace wind::platform
