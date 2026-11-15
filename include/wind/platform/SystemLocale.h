#pragma once

namespace wind::platform {

/// @brief 系统 UI 语言是否为中文（含 zh-CN / zh-TW / zh-HK）。
///
/// 读的是「设置 → 时间和语言 → 语言」里的首选 UI 语言。项目只提供简体文案，
/// 繁体系统也归到中文（显示简体），这样可以避免为了一个界面再维护第三套文案。
bool SystemUsesChineseUi();

}  // namespace wind::platform
