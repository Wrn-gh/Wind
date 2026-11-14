#pragma once

namespace wind::platform {

/// @brief 系统当前是否在使用浅色应用主题。
///
/// 读的是「设置 → 个性化 → 颜色 → 选择默认应用模式」这一项（注册表
/// HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme），
/// 而不是窗口标题栏的明暗。读取失败时按浅色处理，与系统默认值一致。
bool SystemUsesLightTheme();

}  // namespace wind::platform
