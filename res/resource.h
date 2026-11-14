// 编译进 exe 的二进制资源编号。
// 由 res/wind.rc 与 C++ 侧（src/ui/IconSet.cpp）共同引用，因此保持纯 C 宏、不依赖 <windows.h>。
#pragma once

// 程序图标（托盘/窗口用的多尺寸 ICO），由 tools/make_app_icon.py 从 res/logo*.svg 生成。
#define IDI_WIND_ICON 1

#define IDR_WIND_ICON_PIN 101
#define IDR_WIND_ICON_OPACITY 102
#define IDR_WIND_ICON_RESET 103
#define IDR_WIND_ICON_CLOSE 104
