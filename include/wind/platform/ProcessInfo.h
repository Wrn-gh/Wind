#pragma once

#include <windows.h>

#include <string>

/// @brief 平台层：Win32 / DirectX 的直接封装，不含任何业务判断。
namespace wind::platform {

/// @brief 取进程的可执行文件名：小写、不含目录，便于直接比较。
/// @param processId 目标进程 ID。
/// @return 取不到（进程已退出、权限被 UIPI 拒绝）时返回空串。
std::wstring ProcessImageName(DWORD processId);

}  // namespace wind::platform
