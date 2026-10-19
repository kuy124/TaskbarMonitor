cmake_minimum_required(VERSION 3.15)
project(TaskbarMonitor LANGUAGES CXX RC)

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
    src/NetworkRate.h
    src/TemperatureEstimate.h
    src/SensorFeed.h
    src/Metrics.cpp
    src/Renderer.h
    src/Renderer.cpp
    src/TaskbarSync.h
    src/TaskbarPlacement.h
    src/TaskbarSync.cpp
    src/SettingsWindow.h
    src/SettingsWindow.cpp
    src/StartupDialog.h
    src/StartupDialog.cpp
    src/License.h
    src/License.cpp
    src/Main.cpp
    src/Resource.h
    src/TaskbarMonitor.rc
)

set_source_files_properties(src/TaskbarMonitor.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/TaskbarMonitor.ico"
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
    ole32
    oleaut32
    uiautomationcore
    wbemuuid
    winhttp
)

if (MINGW)
    target_compile_options(TaskbarMonitor PRIVATE -municode -mwindows -O3)
    target_link_options(TaskbarMonitor PRIVATE -municode -mwindows)
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(NetworkRateTest tests/NetworkRateTest.cpp)
    target_include_directories(NetworkRateTest PRIVATE src)
    if (MSVC)
        target_compile_options(NetworkRateTest PRIVATE /UNDEBUG)
    else()
        target_compile_options(NetworkRateTest PRIVATE -UNDEBUG)
    endif()
    add_test(NAME NetworkRateTest COMMAND NetworkRateTest)
    add_executable(SensorFeedTest tests/SensorFeedTest.cpp)
    target_include_directories(SensorFeedTest PRIVATE src)
    if (MSVC)
        target_compile_options(SensorFeedTest PRIVATE /UNDEBUG)
    else()
        target_compile_options(SensorFeedTest PRIVATE -UNDEBUG)
    endif()
    add_test(NAME SensorFeedTest COMMAND SensorFeedTest)
    add_executable(TemperatureEstimateTest tests/TemperatureEstimateTest.cpp)
    target_include_directories(TemperatureEstimateTest PRIVATE src)
    if (MSVC)
        target_compile_options(TemperatureEstimateTest PRIVATE /UNDEBUG)
    else()
        target_compile_options(TemperatureEstimateTest PRIVATE -UNDEBUG)
    endif()
    add_test(NAME TemperatureEstimateTest COMMAND TemperatureEstimateTest)
    add_executable(TaskbarPlacementTest tests/TaskbarPlacementTest.cpp)
    target_include_directories(TaskbarPlacementTest PRIVATE src)
    if (MSVC)
        target_compile_options(TaskbarPlacementTest PRIVATE /UNDEBUG)
    else()
        target_compile_options(TaskbarPlacementTest PRIVATE -UNDEBUG)
    endif()
    add_test(NAME TaskbarPlacementTest COMMAND TaskbarPlacementTest)
endif()
