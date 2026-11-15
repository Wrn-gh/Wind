#include "wind/core/SettingsStore.h"

#include <shlobj.h>

#include <algorithm>
#include <cstdio>

#include "wind/core/IgnoreList.h"
#include "wind/platform/Win32Error.h"

namespace wind::core {
namespace {

constexpr const wchar_t* kGeneralSection = L"general";
constexpr const wchar_t* kAppearanceSection = L"appearance";
constexpr const wchar_t* kIgnoreSection = L"ignore";
constexpr const wchar_t* kToggleModsKey = L"toggle_mods";
constexpr const wchar_t* kToggleVkKey = L"toggle_vk";
constexpr const wchar_t* kIgnoreModsKey = L"ignore_mods";
constexpr const wchar_t* kIgnoreVkKey = L"ignore_vk";
constexpr const wchar_t* kQuitNoticeKey = L"quit_hotkey_notice";
constexpr const wchar_t* kAutoStartKey = L"auto_start";
constexpr const wchar_t* kThemeKey = L"theme";
constexpr const wchar_t* kLanguageKey = L"language";
constexpr const wchar_t* kAccentKey = L"accent";
constexpr const wchar_t* kIgnoreCountKey = L"count";

/// 只保留修饰位：MOD_NOREPEAT 之类是注册行为开关，不该写进配置。
constexpr UINT kModifierMask = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

int ReadInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(::GetPrivateProfileIntW(section, key, fallback, path.c_str()));
}

/// @brief 读一个字符串键；容量按「进程名 + 分隔符 + 类名」的最大可能长度给足。
std::wstring ReadString(const std::wstring& path, const wchar_t* section, const wchar_t* key) {
    constexpr size_t kCapacity = IgnoreList::kMaxProcessLength + IgnoreList::kMaxClassLength + 8;
    wchar_t buffer[kCapacity]{};
    ::GetPrivateProfileStringW(section, key, L"", buffer, static_cast<DWORD>(kCapacity), path.c_str());
    return buffer;
}

void WriteInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int value) {
    const std::wstring text = std::to_wstring(value);
    ::WritePrivateProfileStringW(section, key, text.c_str(), path.c_str());
}

/// @brief 删除一个键；传 nullptr 作为值是 profile API 的“删除”写法。
void DeleteKey(const std::wstring& path, const wchar_t* section, const std::wstring& key) {
    ::WritePrivateProfileStringW(section, key.c_str(), nullptr, path.c_str());
}

std::wstring RuleKeyName(size_t index) {
    // 对外用 1 起编号，ini 里读起来更符合习惯。
    return L"rule" + std::to_wstring(index + 1);
}

ThemeMode ParseThemeMode(const std::wstring& text) {
    if (text == L"light") {
        return ThemeMode::Light;
    }
    if (text == L"dark") {
        return ThemeMode::Dark;
    }
    return ThemeMode::System;
}

const wchar_t* ThemeModeText(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light:
            return L"light";
        case ThemeMode::Dark:
            return L"dark";
        case ThemeMode::System:
            break;
    }
    return L"system";
}

Language ParseLanguage(const std::wstring& text) {
    if (text == L"zh") {
        return Language::Chinese;
    }
    if (text == L"en") {
        return Language::English;
    }
    return Language::System;
}

const wchar_t* LanguageText(Language language) {
    switch (language) {
        case Language::Chinese:
            return L"zh";
        case Language::English:
            return L"en";
        case Language::System:
            break;
    }
    return L"system";
}

/// @brief 解析 6 位十六进制颜色；空值 / 非法值一律当作 0（使用主题自带强调色）。
uint32_t ParseAccent(const std::wstring& text) {
    if (text.empty()) {
        return 0;
    }
    wchar_t* end = nullptr;
    const unsigned long value = ::wcstoul(text.c_str(), &end, 16);
    if (end == text.c_str() || value > 0xFFFFFFul) {
        return 0;
    }
    return static_cast<uint32_t>(value);
}

/// @brief 解析 "进程名|类名"；缺分隔符或一侧为空时返回 false。
bool ParseIgnoreRule(const std::wstring& text, IgnoreRule* out) {
    const size_t separator = text.find(IgnoreList::kSeparator);
    if (separator == std::wstring::npos) {
        return false;
    }
    IgnoreRule rule;
    rule.process = text.substr(0, separator);
    rule.className = text.substr(separator + 1);
    if (!rule.IsValid()) {
        return false;
    }
    *out = std::move(rule);
    return true;
}

std::wstring FormatIgnoreRule(const IgnoreRule& rule) {
    return rule.process + IgnoreList::kSeparator + rule.className;
}

}  // namespace

std::wstring SettingsStore::FilePath() {
    PWSTR raw = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &raw))) {
        platform::Log("取不到 %APPDATA% 路径，设置将无法持久化。");
        return {};
    }

    std::wstring path(raw);
    ::CoTaskMemFree(raw);
    path += L"\\Wind\\settings.ini";
    return path;
}

Settings SettingsStore::Load() {
    Settings settings = kDefaultSettings;

    const std::wstring path = FilePath();
    if (path.empty()) {
        return settings;
    }
    if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return settings;
    }

    const int modifiers = static_cast<int>(ReadInt(path, kGeneralSection, kToggleModsKey, -1));
    const int virtualKey = ReadInt(path, kGeneralSection, kToggleVkKey, -1);
    if (modifiers >= 0 && virtualKey > 0) {
        HotkeyBinding binding{static_cast<UINT>(modifiers) & kModifierMask, static_cast<UINT>(virtualKey)};
        // 没有修饰键的组合既注册不上、也会吞掉正常输入，直接当坏数据处理。
        if (IsValidHotkey(binding)) {
            settings.toggleHotkey = binding;
        }
    }

    settings.themeMode = ParseThemeMode(ReadString(path, kAppearanceSection, kThemeKey));
    settings.language = ParseLanguage(ReadString(path, kAppearanceSection, kLanguageKey));
    settings.accentRgb = ParseAccent(ReadString(path, kAppearanceSection, kAccentKey));
    settings.quitHotkeyNoticeShown = ReadInt(path, kGeneralSection, kQuitNoticeKey, 0) != 0;
    settings.autoStart = ReadInt(path, kGeneralSection, kAutoStartKey, 0) != 0;

    const int ignoreModifiers = static_cast<int>(ReadInt(path, kGeneralSection, kIgnoreModsKey, -1));
    const int ignoreVirtualKey = static_cast<int>(ReadInt(path, kGeneralSection, kIgnoreVkKey, -1));
    if (ignoreModifiers >= 0 && ignoreVirtualKey > 0) {
        HotkeyBinding binding{static_cast<UINT>(ignoreModifiers) & kModifierMask,
                              static_cast<UINT>(ignoreVirtualKey)};
        if (IsValidHotkey(binding)) {
            settings.ignoreHotkey = binding;
        }
    }

    // 老配置没有 [ignore] 段，count 缺省为 0，读出来就是空列表，升级后行为零变化。
    const int ruleCount = ReadInt(path, kIgnoreSection, kIgnoreCountKey, 0);
    const size_t readable = ruleCount > 0 ? std::min(static_cast<size_t>(ruleCount), IgnoreList::kMaxRules) : 0;
    settings.ignoredWindows.reserve(readable);
    for (size_t index = 0; index < readable; ++index) {
        const std::wstring key = RuleKeyName(index);
        IgnoreRule rule;
        if (ParseIgnoreRule(ReadString(path, kIgnoreSection, key.c_str()), &rule)) {
            settings.ignoredWindows.push_back(std::move(rule));
        }
    }

    platform::Log("已读取设置：热键=%s，忽略热键=%s，忽略规则=%zu 条，主题=%ls，语言=%ls，强调色=0x%06X，开机自启=%s",
                  FormatHotkey(settings.toggleHotkey).c_str(), FormatHotkey(settings.ignoreHotkey).c_str(),
                  settings.ignoredWindows.size(), ThemeModeText(settings.themeMode),
                  LanguageText(settings.language), settings.accentRgb, settings.autoStart ? "开" : "关");
    return settings;
}

bool SettingsStore::Save(const Settings& settings) {
    const std::wstring path = FilePath();
    if (path.empty()) {
        return false;
    }

    // 目录不存在时 profile API 会静默失败，先补建 %APPDATA%\Wind。
    const size_t separator = path.find_last_of(L'\\');
    if (separator == std::wstring::npos) {
        return false;
    }
    const std::wstring directory = path.substr(0, separator);
    if (::CreateDirectoryW(directory.c_str(), nullptr) == FALSE && ::GetLastError() != ERROR_ALREADY_EXISTS) {
        platform::Log("创建设置目录失败：%s", platform::LastErrorMessage(::GetLastError()).c_str());
        return false;
    }

    WriteInt(path, kGeneralSection, kToggleModsKey, static_cast<int>(settings.toggleHotkey.modifiers & kModifierMask));
    WriteInt(path, kGeneralSection, kToggleVkKey, static_cast<int>(settings.toggleHotkey.virtualKey));
    WriteInt(path, kGeneralSection, kIgnoreModsKey, static_cast<int>(settings.ignoreHotkey.modifiers & kModifierMask));
    WriteInt(path, kGeneralSection, kIgnoreVkKey, static_cast<int>(settings.ignoreHotkey.virtualKey));
    WriteInt(path, kGeneralSection, kQuitNoticeKey, settings.quitHotkeyNoticeShown ? 1 : 0);
    WriteInt(path, kGeneralSection, kAutoStartKey, settings.autoStart ? 1 : 0);
    ::WritePrivateProfileStringW(kAppearanceSection, kThemeKey, ThemeModeText(settings.themeMode), path.c_str());
    ::WritePrivateProfileStringW(kAppearanceSection, kLanguageKey, LanguageText(settings.language), path.c_str());

    wchar_t accent[16]{};
    ::swprintf(accent, 16, L"%06X", settings.accentRgb & 0xFFFFFFu);
    ::WritePrivateProfileStringW(kAppearanceSection, kAccentKey, accent, path.c_str());

    const size_t ruleCount = std::min(settings.ignoredWindows.size(), IgnoreList::kMaxRules);
    WriteInt(path, kIgnoreSection, kIgnoreCountKey, static_cast<int>(ruleCount));
    for (size_t index = 0; index < ruleCount; ++index) {
        const std::wstring key = RuleKeyName(index);
        const std::wstring value = FormatIgnoreRule(settings.ignoredWindows[index]);
        ::WritePrivateProfileStringW(kIgnoreSection, key.c_str(), value.c_str(), path.c_str());
    }
    // 规则变少时清掉多余的行，否则 ini 里会残留已经删掉的规则，下次读 count 虽然不读它，
    // 但手工查看配置文件的人会以为规则还在。
    for (size_t index = ruleCount; index < IgnoreList::kMaxRules; ++index) {
        DeleteKey(path, kIgnoreSection, RuleKeyName(index));
    }

    platform::Log("设置已保存：%ls", path.c_str());
    return true;
}

}  // namespace wind::core
