# TaskbarMonitor

A lightweight, zero-bloat hardware monitor for Windows that sits directly on your taskbar.

Written in pure C++ using direct Win32 APIs, PDH, and GDI. No Electron, no .NET runtime dependencies, no background bloatware—uses under 10 MB of RAM.

---

## Features

- **Network Speed**: Real-time upload (▲) and download (▼) speeds in KB/s or MB/s.
- **CPU & GPU**: Usage percentages using hardware Performance Data Helpers (PDH).
- **RAM**: Memory percentage and currently used gigabytes.
- **Storage**: Real-time disk activity % and free storage space on your chosen drive.
- **System Stats**: Process count and battery percentage (or system uptime on desktops).
- **Adaptive Colors**: Automatically matches Windows Light Mode, Dark Mode, and custom taskbar accent colors.
- **Start Menu Resilient**: Hooks directly to Explorer so it stays pinned and visible even when you open the Start Menu or Search.

---

## Quick Start

1. Download the latest `TaskbarMonitor.exe` from the [Releases](https://github.com/) page.
2. Put `TaskbarMonitor.exe` in any folder you prefer (e.g. `C:\Tools\` or your user folder).
3. Double-click to run it.
   - It will ask whether you want it to run on Windows startup.
   - Click **Yes** if you want it to launch automatically in the background at logon.

---

## Configuration

- **Settings Dialog**: Double-click the system tray icon or right-click it and choose **Settings...**
  - Toggle metrics on/off.
  - Pick which drive to monitor (C:, D:, etc.).
  - Adjust the left margin / position offset.
  - Choose update polling rates (500ms, 1000ms, or 2000ms eco mode).
- **Quick Actions**: Right-click the tray icon to open Windows Task Manager, refresh the theme, or exit.

---

## Building from Source

### Prerequisites
- **MinGW-w64 (GCC 10+)** or **MSVC / Visual Studio**
- Windows SDK

### Build using GCC / MinGW:
```bash
# Option 1: Direct single-line compile
g++ -O3 -mwindows -municode src/Config.cpp src/Theme.cpp src/Metrics.cpp src/Renderer.cpp src/TaskbarSync.cpp src/SettingsWindow.cpp src/Main.cpp -o TaskbarMonitor.exe -liphlpapi -lgdi32 -ldwmapi -lpdh -lpsapi -lshell32 -lcomctl32

# Option 2: Using the Makefile
make
```

### Build using CMake:
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

---

## Project Structure

```text
TaskbarMonitor/
├── src/
│   ├── Common.h          # Global constants, headers, and IDs
│   ├── Config.h/.cpp     # Registry persistence and autostart handler
│   ├── Theme.h/.cpp      # Windows dark/light theme & accent color detection
│   ├── Metrics.h/.cpp    # Hardware polling (CPU, GPU, RAM, Disk, Net, Power)
│   ├── Renderer.h/.cpp   # Double-buffered GDI rendering
│   ├── TaskbarSync.h/.cpp# Window positioning & Shell event hooks
│   ├── SettingsWindow.h/.cpp # Native Win32 configuration window
│   └── Main.cpp          # App entry point, tray icon, and message loop
├── CMakeLists.txt
├── Makefile
└── build.bat
```