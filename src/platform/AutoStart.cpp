#include "wind/platform/AutoStart.h"

#include <string>
#include <vector>

#include "wind/platform/Win32Error.h"

namespace wind::platform {
namespace {

/// 登录时运行的程序清单。改这里的路径会被 UAC 虚拟化挡下来，所以必须用 HKCU 本体。
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
/// 任务管理器「启动」页的启用/禁用状态；它不删 Run 项，只在这里留一个标记。
constexpr const wchar_t* kApprovedKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

/// 展开 REG_EXPAND_SZ 里的 %VAR%；没有百分号时原样返回。
std::wstring ExpandEnvironment(const std::wstring& text) {
    if (text.find(L'%') == std::wstring::npos) {
        return text;
    }
    const DWORD needed = ::ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
    if (needed == 0) {
        return text;
    }
    std::wstring expanded(needed, L'\0');
    const DWORD written = ::ExpandEnvironmentStringsW(text.c_str(), expanded.data(), needed);
    if (written == 0 || written > needed) {
        return text;
    }
    expanded.resize(written - 1);  // 返回值含结尾的 '\0'
    return expanded;
}

/// @brief 剥掉首尾空白与成对引号，补成绝对路径，供比较使用。
/// @note 引号不成对时保留原样，比较自然失败并触发一次覆写，比猜错更安全。
std::wstring NormalizeCommand(const std::wstring& raw) {
    std::wstring text = ExpandEnvironment(raw);

    const size_t first = text.find_first_not_of(L" \t");
    text.erase(0, (first == std::wstring::npos) ? text.size() : first);
    while (!text.empty() && (text.back() == L' ' || text.back() == L'\t')) {
        text.pop_back();
    }

    if (text.size() >= 2 && text.front() == L'"') {
        const size_t closing = text.find(L'"', 1);
        if (closing == std::wstring::npos) {
            return text;
        }
        text = text.substr(1, closing - 1);
    }
    if (text.empty()) {
        return {};
    }

    const DWORD needed = ::GetFullPathNameW(text.c_str(), 0, nullptr, nullptr);
    if (needed == 0) {
        return text;
    }
    std::wstring full(needed, L'\0');
    const DWORD written = ::GetFullPathNameW(text.c_str(), needed, full.data(), nullptr);
    if (written == 0 || written >= needed) {
        return text;
    }
    full.resize(written);
    return full;
}

/// @brief 读取 Run 键下的字符串值；不存在不算错误，返回 false。
bool ReadRunValue(const wchar_t* valueName, std::wstring& out) {
    HKEY key = nullptr;
    LSTATUS status = ::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        if (status != ERROR_FILE_NOT_FOUND) {
            Log("打开 Run 键失败：%s", LastErrorMessage(static_cast<unsigned long>(status)).c_str());
        }
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    status = ::RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
        ::RegCloseKey(key);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
            Log("读取自启项失败：%s", LastErrorMessage(static_cast<unsigned long>(status)).c_str());
        }
        return false;
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    status = ::RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        Log("读取自启项失败：%s", LastErrorMessage(static_cast<unsigned long>(status)).c_str());
        return false;
    }

    out.assign(buffer.data(), size / sizeof(wchar_t));
    while (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return true;
}

/// @brief StartupApproved 里是否被标记成「禁用」。
///
/// 该值是 12 字节 BLOB，首字节 02 / 06 / 01 为启用、03 / 07 / 0B 为禁用，
/// 也就是低两位为 3 即禁用；任务管理器「启用」等于把这条值删掉。
bool IsApprovedDisabled(const wchar_t* valueName) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;  // 没有这个键就是从未被禁用过
    }

    DWORD type = 0;
    BYTE data[32]{};
    DWORD size = sizeof(data);
    const LSTATUS status = ::RegQueryValueExW(key, valueName, nullptr, &type, data, &size);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_BINARY || size == 0) {
        return false;
    }
    return (data[0] & 0x03) == 0x03;
}

/// @brief 删掉 StartupApproved 的禁用标记，让 Run 项真的会生效。
void ClearApprovedDisabled(const wchar_t* valueName) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }
    ::RegDeleteValueW(key, valueName);
    ::RegCloseKey(key);
}

}  // namespace

std::wstring ExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            Log("取不到自身 exe 路径：%s", LastErrorMessage(::GetLastError()).c_str());
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        // 返回值等于缓冲区容量说明被截断了，加倍重试。
        if (path.size() >= 32768) {
            Log("自身 exe 路径超过 32768 个字符，放弃读取。");
            return {};
        }
        path.resize(path.size() * 2);
    }
}

AutoStartState QueryAutoStart(const wchar_t* valueName, std::wstring* storedCommand) {
    std::wstring stored;
    if (!ReadRunValue(valueName, stored)) {
        return AutoStartState::Off;
    }
    if (storedCommand != nullptr) {
        *storedCommand = stored;
    }

    if (IsApprovedDisabled(valueName)) {
        return AutoStartState::Off;
    }

    const std::wstring executable = ExecutablePath();
    if (executable.empty()) {
        // 连自己的路径都拿不到就没法判断，当作需要覆写。
        return AutoStartState::OnStale;
    }

    const std::wstring existing = NormalizeCommand(stored);
    if (existing.empty() ||
        ::CompareStringOrdinal(existing.c_str(), -1, executable.c_str(), -1, TRUE) != CSTR_EQUAL) {
        return AutoStartState::OnStale;
    }
    return AutoStartState::On;
}

bool SetAutoStart(const wchar_t* valueName) {
    const std::wstring executable = ExecutablePath();
    if (executable.empty()) {
        Log("开机自启未写入：取不到自身路径。");
        return false;
    }

    // Run 的值会被按 CreateProcess 规则解析，不带引号时 "C:\Program Files\..." 会被
    // 截成 "C:\Program.exe" 去执行，所以引号是必须的，不是美观问题。
    const std::wstring command = L"\"" + executable + L"\"";

    HKEY key = nullptr;
    LSTATUS status = ::RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                       nullptr);
    if (status != ERROR_SUCCESS) {
        Log("打开 Run 键写入自启失败：%s", LastErrorMessage(static_cast<unsigned long>(status)).c_str());
        return false;
    }

    status = ::RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                              static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        Log("写入自启项失败：%s", LastErrorMessage(static_cast<unsigned long>(status)).c_str());
        return false;
    }

    ClearApprovedDisabled(valueName);
    Log("开机自启已写入：%ls", command.c_str());
    return true;
}

bool RemoveAutoStart(const wchar_t* valueName) {
    HKEY key = nullptr;
    LSTATUS status = ::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND) {
        ClearApprovedDisabled(valueName);
        return true;
    }
    if (status != ERROR_SUCCESS) {
        Log("打开 Run 键删除自启失败：%s", LastErrorMessage(static_cast<unsigned long>(status)).c_str());
        return false;
    }

    status = ::RegDeleteValueW(key, valueName);
    ::RegCloseKey(key);
    // 本来就没有这一项，结果与目标一致。
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        Log("删除自启项失败：%s", LastErrorMessage(static_cast<unsigned long>(status)).c_str());
        return false;
    }

    ClearApprovedDisabled(valueName);
    Log("开机自启已删除。");
    return true;
}

}  // namespace wind::platform
