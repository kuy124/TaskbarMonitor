cmake_minimum_required(VERSION 3.15)
project(TaskbarMonitor LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SENSOR_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sensors")
add_custom_command(OUTPUT "${SENSOR_DIRECTORY}/EmbeddedSensors.rc"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_sensors.ps1" -Output "${SENSOR_DIRECTORY}"
    DEPENDS tools/build_sensors.ps1 src/SensorBridge.cs src/AdditionalSensors.cs
    VERBATIM)
add_custom_target(SensorBundle DEPENDS "${SENSOR_DIRECTORY}/EmbeddedSensors.rc")
include_directories("${SENSOR_DIRECTORY}")

add_definitions(-DUNICODE -D_UNICODE)

set(SOURCES
    src/Common.h
    src/Config.h
    src/Config.cpp
    src/Theme.h
    src/Theme.cpp
    src/Metrics.h
    src/GpuUsage.h
    src/NetworkRate.h
    src/TemperatureEstimate.h
    src/HardwareSensors.h
    src/HardwareSensors.cpp
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
    src/Updater.cpp
    src/Elevation.cpp
    src/Elevation.h
    src/Resource.h
    src/TaskbarMonitor.rc
)

set_source_files_properties(src/TaskbarMonitor.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/TaskbarMonitor.ico;${CMAKE_CURRENT_SOURCE_DIR}/src/TaskbarMonitor.manifest;${CMAKE_CURRENT_SOURCE_DIR}/src/Updater.ps1;${SENSOR_DIRECTORY}/EmbeddedSensors.rc"
)

add_executable(TaskbarMonitor WIN32 ${SOURCES})
add_dependencies(TaskbarMonitor SensorBundle)

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
)

if (MINGW)
    target_compile_options(TaskbarMonitor PRIVATE -municode -mwindows -O3)
    target_link_options(TaskbarMonitor PRIVATE -municode -mwindows -static)
endif()

include(CTest)
if(BUILD_TESTING AND WIN32)
    add_test(NAME UpdaterTests COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/src/Updater.ps1" -SelfTest)
endif()
if (BUILD_TESTING)
    add_executable(NetworkRateTest tests/NetworkRateTest.cpp)
    target_include_directories(NetworkRateTest PRIVATE src)
    if (MSVC)
        target_compile_options(NetworkRateTest PRIVATE /UNDEBUG)
    else()
        target_compile_options(NetworkRateTest PRIVATE -UNDEBUG)
    endif()
    add_test(NAME NetworkRateTest COMMAND NetworkRateTest)
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
    add_executable(TaskbarStartupTest tests/TaskbarStartupTest.cpp src/TaskbarSync.cpp src/TaskbarMonitor.rc)
    add_dependencies(TaskbarStartupTest SensorBundle)
    target_include_directories(TaskbarStartupTest PRIVATE src)
    target_link_libraries(TaskbarStartupTest PRIVATE ole32 oleaut32 uiautomationcore)
    if (MSVC)
        target_compile_options(TaskbarStartupTest PRIVATE /UNDEBUG)
    else()
        target_compile_options(TaskbarStartupTest PRIVATE -UNDEBUG)
    endif()
    add_test(NAME TaskbarStartupTest COMMAND TaskbarStartupTest)
    add_executable(MetricsTest tests/MetricsTest.cpp src/HardwareSensors.cpp src/SensorResources.rc src/SettingsWindow.cpp src/Theme.cpp src/Elevation.cpp)
    add_dependencies(MetricsTest SensorBundle)
    set_property(SOURCE src/SensorResources.rc APPEND PROPERTY OBJECT_DEPENDS "${SENSOR_DIRECTORY}/EmbeddedSensors.rc")
    target_include_directories(MetricsTest PRIVATE src)
    get_target_property(TASKBAR_MONITOR_LIBRARIES TaskbarMonitor LINK_LIBRARIES)
    target_link_libraries(MetricsTest PRIVATE ${TASKBAR_MONITOR_LIBRARIES})
    if (MSVC)
        target_compile_options(MetricsTest PRIVATE /UNDEBUG)
    else()
        target_compile_options(MetricsTest PRIVATE -UNDEBUG)
    endif()
    add_test(NAME MetricsTest COMMAND MetricsTest)
    add_executable(ElevationTest tests/ElevationTest.cpp)
    target_include_directories(ElevationTest PRIVATE src)
    target_link_libraries(ElevationTest PRIVATE ${TASKBAR_MONITOR_LIBRARIES})
    if (MSVC)
        target_compile_options(ElevationTest PRIVATE /UNDEBUG)
    else()
        target_compile_options(ElevationTest PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ElevationTest COMMAND ElevationTest)
    set_tests_properties(ElevationTest PROPERTIES TIMEOUT 45)
    set_tests_properties(MetricsTest UpdaterTests PROPERTIES TIMEOUT 60)
    add_test(NAME SingleInstanceTest COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/SingleInstanceTest.ps1"
        -Executable "$<TARGET_FILE:TaskbarMonitor>")
endif()
