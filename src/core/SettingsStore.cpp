#include "wind/core/SettingsStore.h"

#include <shlobj.h>

#include <cstdio>

#include "wind/platform/Win32Error.h"

namespace wind::core {
namespace {

constexpr const wchar_t* kGeneralSection = L"general";
constexpr const wchar_t* kAppearanceSection = L"appearance";
constexpr const wchar_t* kToggleModsKey = L"toggle_mods";
constexpr const wchar_t* kToggleVkKey = L"toggle_vk";
constexpr const wchar_t* kQuitNoticeKey = L"quit_hotkey_notice";
constexpr const wchar_t* kAutoStartKey = L"auto_start";
constexpr const wchar_t* kThemeKey = L"theme";
constexpr const wchar_t* kAccentKey = L"accent";

/// 只保留修饰位：MOD_NOREPEAT 之类是注册行为开关，不该写进配置。
constexpr UINT kModifierMask = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

int ReadInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(::GetPrivateProfileIntW(section, key, fallback, path.c_str()));
}

std::wstring ReadString(const std::wstring& path, const wchar_t* section, const wchar_t* key) {
    wchar_t buffer[64]{};
    ::GetPrivateProfileStringW(section, key, L"", buffer, 64, path.c_str());
    return buffer;
}

void WriteInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int value) {
    const std::wstring text = std::to_wstring(value);
    ::WritePrivateProfileStringW(section, key, text.c_str(), path.c_str());
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
    settings.accentRgb = ParseAccent(ReadString(path, kAppearanceSection, kAccentKey));
    settings.quitHotkeyNoticeShown = ReadInt(path, kGeneralSection, kQuitNoticeKey, 0) != 0;
    settings.autoStart = ReadInt(path, kGeneralSection, kAutoStartKey, 0) != 0;

    platform::Log("已读取设置：热键=%s，主题=%ls，强调色=0x%06X，开机自启=%s",
                  FormatHotkey(settings.toggleHotkey).c_str(), ThemeModeText(settings.themeMode),
                  settings.accentRgb, settings.autoStart ? "开" : "关");
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
    WriteInt(path, kGeneralSection, kQuitNoticeKey, settings.quitHotkeyNoticeShown ? 1 : 0);
    WriteInt(path, kGeneralSection, kAutoStartKey, settings.autoStart ? 1 : 0);
    ::WritePrivateProfileStringW(kAppearanceSection, kThemeKey, ThemeModeText(settings.themeMode), path.c_str());

    wchar_t accent[16]{};
    ::swprintf(accent, 16, L"%06X", settings.accentRgb & 0xFFFFFFu);
    ::WritePrivateProfileStringW(kAppearanceSection, kAccentKey, accent, path.c_str());

    platform::Log("设置已保存：%ls", path.c_str());
    return true;
}

}  // namespace wind::core
