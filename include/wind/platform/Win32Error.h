#pragma once

#include <windows.h>

#include <string>

/// @brief 平台层：只依赖 Win32 / DX，不含任何业务语义。
namespace wind::platform {

/// @brief 把 GetLastError() 的错误码格式化成 UTF-8 可读消息。
/// @param error 错误码，通常来自 GetLastError()。
/// @return 形如 “拒绝访问。(0x00000005)” 的文本；系统无对应描述时只返回十六进制码。
std::string LastErrorMessage(unsigned long error);

/// @brief 把 HRESULT 格式化成 UTF-8 可读消息，规则同 LastErrorMessage()。
/// @param hr HRESULT 值（按 long 传递，避免把 <winerror.h> 泄漏到调用方）。
std::string HResultMessage(long hr);

/// @brief 输出一条诊断日志：写入调试器输出，并追加到 %TEMP%\Wind.log。
/// 本工具是 GUI 程序且不带控制台，日志文件是排查失败原因的唯一出口。
/// @param format UTF-8 格式化串，参数语义同 printf。
void Log(const char* format, ...);

}  // namespace wind::platform
