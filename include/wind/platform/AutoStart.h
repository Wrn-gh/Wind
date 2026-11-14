#pragma once

#include <windows.h>

#include <string>

namespace wind::platform {

/// @brief 开机自启在 HKCU\...\Run 下使用的值名。
///
/// 任务管理器「启动」页显示的就是这个名字，改掉会让用户看到两个条目。
inline constexpr const wchar_t* kAutoStartValueName = L"Wind";

/// @brief 自启项的三种状态。
enum class AutoStartState {
    /// @brief 注册表里没有这一项，或被任务管理器标记为禁用。
    Off,
    /// @brief 项存在、未被禁用，且指向当前这个 exe。
    On,
    /// @brief 项存在、未被禁用，但指向别处（用户移动过 exe，或旧版本的残留路径）。
    OnStale,
};

/// @brief 当前进程 exe 的完整路径；取不到时返回空串。
/// @note 用倍增缓冲区读取，避免超过 MAX_PATH 的路径被静默截断成错误的目录。
std::wstring ExecutablePath();

/// @brief 查询自启项状态。
/// @param valueName Run 键下的值名，一般传 kAutoStartValueName。
/// @param storedCommand 非空时回传注册表里的原始数据（含引号），便于日志排查。
AutoStartState QueryAutoStart(const wchar_t* valueName, std::wstring* storedCommand = nullptr);

/// @brief 把自启项写成当前 exe 的完整路径。
/// @note 值带引号，否则含空格的路径会被按 CreateProcess 规则截断执行。
///       同时清掉任务管理器的禁用标记，否则写了也不会真的启动。
/// @return 写入成功返回 true。
bool SetAutoStart(const wchar_t* valueName);

/// @brief 删除自启项（连同任务管理器的禁用标记）。
/// @note 本来就不存在视为成功，因此可以反复调用。
bool RemoveAutoStart(const wchar_t* valueName);

}  // namespace wind::platform
