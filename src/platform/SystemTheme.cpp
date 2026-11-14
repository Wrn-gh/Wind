#include "wind/platform/SystemTheme.h"

#include <windows.h>

namespace wind::platform {
namespace {

constexpr wchar_t kPersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr wchar_t kAppsUseLightTheme[] = L"AppsUseLightTheme";

}  // namespace

bool SystemUsesLightTheme() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS status = ::RegGetValueW(HKEY_CURRENT_USER, kPersonalizeKey, kAppsUseLightTheme,
                                          RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status != ERROR_SUCCESS) {
        // 该项在精简系统里可能不存在，此时沿用 Windows 的默认（浅色），不当作错误。
        return true;
    }
    return value != 0;
}

}  // namespace wind::platform
