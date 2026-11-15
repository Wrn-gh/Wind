cmake_minimum_required(VERSION 3.25)

# 版本号的唯一来源：默认值给本地开发用，CI 用 git tag 通过 -DWIND_VERSION= 覆盖。
# 允许带预发布后缀（如 1.2.0-rc1），但前三段数字必须能取出来给 VERSIONINFO 用。
set(WIND_VERSION "1.0.0" CACHE STRING "产品版本号（CI 用 git tag 覆盖）")
if(NOT WIND_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    message(FATAL_ERROR "WIND_VERSION 必须形如 主.次.修订[-后缀]，当前是 \"${WIND_VERSION}\"")
endif()
set(WIND_VERSION_NUMERIC "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set(WIND_VERSION_COMMA "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")

project(Wind VERSION ${WIND_VERSION_NUMERIC} LANGUAGES CXX RC)

# exe 的版本资源（属性 → 详细信息）。生成到构建目录，避免污染源码树。
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/res/wind_version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/wind_version.h
    @ONLY
)
message(STATUS "Wind 版本：${WIND_VERSION}（VERSIONINFO ${WIND_VERSION_COMMA}）")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 本项目使用静态三元组（x64-windows-static），第三方库按 /MT 编译，
# 这里显式对齐运行库，避免 LNK4098（LIBCMT 与 MSVCRT 冲突）。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 依赖由 vcpkg manifest 模式提供（见 vcpkg.json）
find_package(imgui CONFIG REQUIRED)

add_executable(Wind WIN32
    src/app/main.cpp
    src/app/Application.cpp
    src/platform/Win32Error.cpp
    src/platform/AutoStart.cpp
    src/platform/Dpi.cpp
    src/platform/Hotkey.cpp
    src/platform/InputHook.cpp
    src/platform/Renderer.cpp
    src/platform/SingleInstance.cpp
    src/platform/SystemTheme.cpp
    src/platform/SystemLocale.cpp
    src/platform/Text.cpp
    src/platform/ProcessInfo.cpp
    src/core/TargetWindow.cpp
    src/core/WindowProperties.cpp
    src/core/WindowRegistry.cpp
    src/core/IgnoreList.cpp
    src/core/Selection.cpp
    src/core/Settings.cpp
    src/core/SettingsStore.cpp
    src/ui/OverlayWindow.cpp
    src/ui/ToolbarView.cpp
    src/ui/HintBar.cpp
    src/ui/IgnorePickerView.cpp
    src/ui/IconSet.cpp
    src/ui/SettingsView.cpp
    src/ui/SettingsWindow.cpp
    src/ui/TrayIcon.cpp
    src/ui/Text.cpp
    res/wind.rc
)

target_include_directories(Wind PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # 图标资源 ID 头文件（resource.h）放在 res 下，.rc 与 C++ 共用
    ${CMAKE_CURRENT_SOURCE_DIR}/res
    # 版本号头文件（wind_version.h）由 configure_file 生成，wind.rc 从这里取
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_definitions(Wind PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    # /utf-8：源码与执行字符集统一为 UTF-8，中文字面量与 ImGui 文本才能正确显示
    target_compile_options(Wind PRIVATE /W4 /permissive- /utf-8 /EHsc)
    set_property(TARGET Wind PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

target_link_libraries(Wind PRIVATE
    imgui::imgui
    # 注册表 API：开机自启项读写
    advapi32
    d3d11
    dxgi
    dcomp
    dwmapi
    shcore
    user32
    gdi32
    shell32
    ole32
    # WIC：把内嵌的 PNG 图标解码成位图
    windowscodecs
)
