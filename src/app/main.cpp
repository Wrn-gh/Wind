#include <windows.h>

#include "wind/app/Application.h"

/// @brief 进程入口。真正的逻辑都在 app::Application 里，这里只做转接。
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*previousInstance*/, PWSTR /*commandLine*/, int /*showCommand*/) {
    wind::app::Application application;
    return application.Run(instance);
}
