#include "wind/platform/SingleInstance.h"

#include <cwchar>
#include <iterator>

#include "wind/platform/Win32Error.h"

namespace wind::platform {
namespace {

constexpr const wchar_t* kMutexName = L"Local\\Wind.SingleInstance";
constexpr const wchar_t* kRequestMessageName = L"Wind.RequestShowSettings";
constexpr const wchar_t* kQuitMessageName = L"Wind.RequestQuit";
/// 托盘宿主窗口的类名：已有实例随时都有这个窗口（即使托盘图标注册失败），
/// 所以拿它当唤醒消息的落点，比找叠加层或设置页窗口可靠。
constexpr const wchar_t* kTrayWindowClass = L"WindTrayHostWindow";

/// 唤醒信号的等待预算：新实例启动时旧实例可能刚好还在初始化，重试几次再放弃。
constexpr int kRequestAttempts = 10;
constexpr DWORD kRequestRetryDelayMs = 50;

/// 等对方退出的上限：正常退出是毫秒级的，超时多半是被别的东西卡住了。
constexpr DWORD kQuitTimeoutMs = 5000;

struct TrayWindowSearch {
    DWORD excludePid;
    HWND result;
};

BOOL CALLBACK FindTrayWindow(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<TrayWindowSearch*>(param);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    // 认窗口只认别的进程的，避免撞上自己。
    if (pid == search->excludePid || pid == 0) {
        return TRUE;
    }
    wchar_t name[64]{};
    if (::GetClassNameW(hwnd, name, static_cast<int>(std::size(name))) == 0) {
        return TRUE;
    }
    if (::wcscmp(name, kTrayWindowClass) != 0) {
        return TRUE;
    }
    search->result = hwnd;
    return FALSE;
}

/// @brief 找到已有实例的托盘宿主窗口并把它要的消息投进去。
/// @return true 表示消息已经投出。
bool PostToExistingTrayWindow(UINT message, const char* logOnSuccess, const char* logOnFailure) {
    if (message == 0) {
        platform::Log("窗口消息注册失败，无法通知已有实例。");
        return false;
    }

    for (int attempt = 0; attempt < kRequestAttempts; ++attempt) {
        TrayWindowSearch search{::GetCurrentProcessId(), nullptr};
        ::EnumWindows(&FindTrayWindow, reinterpret_cast<LPARAM>(&search));
        if (search.result != nullptr) {
            ::PostMessageW(search.result, message, 0, 0);
            platform::Log("%s", logOnSuccess);
            return true;
        }
        ::Sleep(kRequestRetryDelayMs);
    }
    platform::Log("%s", logOnFailure);
    return false;
}

}  // namespace

SingleInstance::~SingleInstance() { Release(); }

UINT SingleInstance::RequestMessage() {
    // 函数内静态量只注册一次，且 C++11 起初始化线程安全。
    static const UINT message = ::RegisterWindowMessageW(kRequestMessageName);
    return message;
}

UINT SingleInstance::QuitMessage() {
    static const UINT message = ::RegisterWindowMessageW(kQuitMessageName);
    return message;
}

bool SingleInstance::RequestExistingQuit() {
    // 先看互斥体在不在：没有实例时连窗口都不用找，直接算成功。
    HANDLE existing = ::OpenMutexW(SYNCHRONIZE, FALSE, kMutexName);
    if (existing == nullptr) {
        return true;
    }

    // 找不到托盘窗口就只等超时——对方可能卡在初始化里，不能因此永远等下去。
    PostToExistingTrayWindow(QuitMessage(), "已请求已有实例正常退出。", "已有实例在运行，但没找到它的托盘窗口。");
    const DWORD result = ::WaitForSingleObject(existing, kQuitTimeoutMs);
    ::CloseHandle(existing);
    if (result != WAIT_OBJECT_0) {
        platform::Log("等待已有实例退出超时。");
        return false;
    }
    return true;
}

bool SingleInstance::Acquire() {
    if (mutex_ != nullptr) {
        return true;
    }

    // bInitialOwner = TRUE：第一个进程创建即持有，第二个进程只会拿到“已存在”。
    mutex_ = ::CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex_ == nullptr) {
        // 句柄都拿不到属于极端情况，放行总比让用户彻底打不开程序好。
        platform::Log("单实例互斥体创建失败：%s", platform::LastErrorMessage(::GetLastError()).c_str());
        return true;
    }
    if (::GetLastError() != ERROR_ALREADY_EXISTS) {
        return true;
    }

    // 这个句柄指向的是别人的互斥体，留着只会延长它的寿命，立刻关掉。
    ::CloseHandle(mutex_);
    mutex_ = nullptr;
    PostToExistingTrayWindow(RequestMessage(), "已有实例在运行，已请求它打开设置页。",
                             "已有实例在运行，但没有找到它的托盘窗口，无法唤醒。");
    return false;
}

void SingleInstance::Release() {
    if (mutex_ == nullptr) {
        return;
    }
    ::ReleaseMutex(mutex_);
    ::CloseHandle(mutex_);
    mutex_ = nullptr;
}

}  // namespace wind::platform
