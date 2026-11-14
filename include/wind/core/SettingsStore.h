#pragma once

#include <string>

#include "wind/core/Settings.h"

namespace wind::core {

/// @brief 设置的持久化：读写 %APPDATA%\Wind\settings.ini。
///
/// 用 Win32 自带的 GetPrivateProfileIntW / WritePrivateProfileStringW，
/// 不引第三方 ini 库：设置项只有四五个标量，profile API 足够且零依赖。
/// 读取一律带校验，坏文件不允许让程序起不来 —— 任何异常都回落到默认值。
class SettingsStore {
public:
    /// @brief 配置文件完整路径；取不到 %APPDATA% 时返回空串。
    static std::wstring FilePath();

    /// @brief 读取设置；文件不存在或某项非法时该项回落默认值。
    static Settings Load();

    /// @brief 写入设置（目录不存在会自动创建）。
    /// @return 成功返回 true；失败只记录日志，调用方不必中断流程。
    static bool Save(const Settings& settings);
};

}  // namespace wind::core
