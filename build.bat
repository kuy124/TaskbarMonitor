@echo off
setlocal enabledelayedexpansion

echo [BUILD] Compiling embedded Windows manifest and version resources...
windres -i src/TaskbarMonitor.rc -o src/TaskbarMonitor.res.o

echo [BUILD] Compiling TaskbarMonitor C++ sources...
g++ -O3 -mwindows -municode src/Config.cpp src/Theme.cpp src/Metrics.cpp src/Renderer.cpp src/TaskbarSync.cpp src/SettingsWindow.cpp src/StartupDialog.cpp src/License.cpp src/Main.cpp src/TaskbarMonitor.res.o -o TaskbarMonitor.exe -liphlpapi -lgdi32 -ldwmapi -lpdh -lpsapi -lshell32 -lcomctl32 -lcomdlg32 -lole32 -loleaut32 -luiautomationcore -luuid -lwbemuuid -lwinhttp

if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    exit /b %errorlevel%
)

echo [OK] Successfully compiled TaskbarMonitor.exe!
echo [SIGN] Applying local code signature to prevent Unknown Publisher security warnings...

powershell -NoProfile -ExecutionPolicy Bypass -File tools\sign.ps1 TaskbarMonitor.exe
