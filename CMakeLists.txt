cmake_minimum_required(VERSION 3.15)
project(TaskbarMonitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DUNICODE -D_UNICODE)

set(SOURCES
    src/Common.h
    src/Config.h
    src/Config.cpp
    src/Theme.h
    src/Theme.cpp
    src/Metrics.h
    src/Metrics.cpp
    src/Renderer.h
    src/Renderer.cpp
    src/TaskbarSync.h
    src/TaskbarSync.cpp
    src/SettingsWindow.h
    src/SettingsWindow.cpp
    src/Main.cpp
)

add_executable(TaskbarMonitor WIN32 ${SOURCES})

target_include_directories(TaskbarMonitor PRIVATE src)

target_link_libraries(TaskbarMonitor PRIVATE
    iphlpapi
    gdi32
    dwmapi
    pdh
    psapi
    shell32
    comctl32
    comdlg32
)

if (MINGW)
    target_compile_options(TaskbarMonitor PRIVATE -municode -mwindows -O3)
    target_link_options(TaskbarMonitor PRIVATE -municode -mwindows)
endif()