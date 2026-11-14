#pragma once

#include <windows.h>

namespace wind::platform {

/// @brief 单实例守卫：命名互斥体保证同一个登录会话里只跑一个 Wind。
///
/// 用 Local\ 前缀而不是 Global\：作用域是当前登录会话，多用户同时登录时各有一份，
/// 不会因为别的用户开着 Wind 就让本用户启动不了。
///
/// 抢占失败的一方不做任何事就退出，但会先给已有实例发一条注册消息，
/// 让它的设置页跳到前台——用户双击图标才不至于毫无反馈。
class SingleInstance final {
public:
    SingleInstance() = default;
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    /// @brief 尝试取得唯一实例身份。
    /// @return true 表示本进程是唯一实例；false 表示已有实例在运行，
    ///         该情况下已顺手请求它打开设置页，调用方直接退出即可。
    bool Acquire();

    /// @brief 放开互斥体，允许后续进程启动；可重复调用。
    void Release();

    /// @brief 已有实例用来接收“请把设置页提出来”的窗口消息 ID。
    /// @note 两个进程用同一个名字向系统注册，拿到的 ID 必然相同。
    static UINT RequestMessage();

    /// @brief 已有实例用来接收“请正常退出”的窗口消息 ID。
    static UINT QuitMessage();

    /// @brief 请已有实例正常退出，并等它释放互斥体。
    ///
    /// 安装包在覆盖文件前必须让 Wind 先走完退出流程，否则被强杀的进程来不及还原
    /// 它改过的窗口。等到对方退出（或超时）才返回，调用方无需自己轮询。
    ///
    /// @return true 表示此刻已经没有实例在运行（本来就没有，或它已经退出）。
    static bool RequestExistingQuit();

private:
    HANDLE mutex_ = nullptr;
};

}  // namespace wind::platform
