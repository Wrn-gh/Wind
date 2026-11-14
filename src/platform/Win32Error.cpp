#include "wind/platform/Win32Error.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace wind::platform {
namespace {

/// 系统消息统一转成 UTF-8，日志文件与 ImGui 文本共用同一种编码。
std::string WideToUtf8(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<size_t>(size - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::string FormatCode(unsigned long code) {
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", code);
    return buffer;
}

}  // namespace

std::string LastErrorMessage(unsigned long error) {
    wchar_t* raw = nullptr;
    const DWORD flags =
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length =
        ::FormatMessageW(flags, nullptr, error, 0, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);

    std::string message;
    if (length > 0 && raw != nullptr) {
        std::wstring text(raw, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
            text.pop_back();
        }
        message = WideToUtf8(text.c_str());
    }
    if (raw != nullptr) {
        ::LocalFree(raw);
    }

    const std::string code = FormatCode(error);
    return message.empty() ? code : message + " (" + code + ")";
}

std::string HResultMessage(long hr) {
    return LastErrorMessage(static_cast<unsigned long>(hr));
}

void Log(const char* format, ...) {
    char message[2048]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    SYSTEMTIME now{};
    ::GetLocalTime(&now);
    char line[2200]{};
    std::snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] %s\n", now.wHour, now.wMinute, now.wSecond,
                  now.wMilliseconds, message);

    ::OutputDebugStringA(line);

    wchar_t directory[MAX_PATH]{};
    const DWORD length = ::GetTempPathW(MAX_PATH, directory);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }
    std::wstring path(directory);
    path += L"Wind.log";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"ab") == 0 && file != nullptr) {
        std::fwrite(line, 1, std::strlen(line), file);
        std::fclose(file);
    }
}

}  // namespace wind::platform
