#pragma once

#include <string>

namespace wind::platform {

/// @brief UTF-8 窄串 → 宽串；内容非法时返回空串。
std::wstring Widen(const std::string& text);

/// @brief 宽串 → UTF-8 窄串；内容非法时返回空串。
///
/// 界面文字统一以 UTF-8 存放：ImGui 只吃 const char*，而 Win32 的菜单、气泡、
/// 消息框只吃 wchar_t，两个方向都要有，集中在这里省得各层各写一份。
std::string Narrow(const std::wstring& text);

}  // namespace wind::platform
