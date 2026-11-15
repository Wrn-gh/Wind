#include "wind/platform/ProcessInfo.h"

#include <algorithm>
#include <iterator>

namespace wind::platform {
namespace {

/// 长路径（\\?\ 前缀）可能超过 MAX_PATH，留足空间后一个 exe 名足够装下。
constexpr size_t kPathCapacity = 1024;

}  // namespace

std::wstring ProcessImageName(DWORD processId) {
    if (processId == 0) {
        return {};
    }

    // PROCESS_QUERY_LIMITED_INFORMATION 比 QUERY_INFORMATION 要求低得多：
    // 前台普通程序全都够用，也不需要提升权限。
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        return {};
    }

    wchar_t path[kPathCapacity]{};
    DWORD length = static_cast<DWORD>(kPathCapacity);
    const BOOL ok = ::QueryFullProcessImageNameW(process, 0, path, &length);
    ::CloseHandle(process);
    if (ok == FALSE || length == 0) {
        return {};
    }

    std::wstring name(path, length);
    const size_t separator = name.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        name.erase(0, separator + 1);
    }
    // 规则比较统一用小写："Explorer.EXE" 与 "explorer.exe" 不该算两个进程。
    std::transform(name.begin(), name.end(), name.begin(), ::towlower);
    return name;
}

}  // namespace wind::platform
