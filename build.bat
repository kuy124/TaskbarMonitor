@echo off
g++ -O3 -mwindows -municode src/Config.cpp src/Theme.cpp src/Metrics.cpp src/Renderer.cpp src/TaskbarSync.cpp src/SettingsWindow.cpp src/Main.cpp -o TaskbarMonitor.exe -liphlpapi -lgdi32 -ldwmapi -lpdh -lpsapi -lshell32 -lcomctl32
if %errorlevel% equ 0 (
    echo [OK] Successfully built TaskbarMonitor.exe
) else (
    echo [ERROR] Build failed.
)