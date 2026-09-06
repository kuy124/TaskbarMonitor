<div align="center">

# TaskbarMonitor

**A ultra-lightweight, native Windows hardware monitor that renders directly inside your taskbar.**

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6?logo=windows&logoColor=white)](https://github.com/)
[![Language](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=c%2B%2B&logoColor=white)](https://en.cppreference.com/w/cpp/17)
[![Architecture](https://img.shields.io/badge/Win32-Native%20GDI-4CAF50)](https://docs.microsoft.com/en-us/windows/win32/)
[![RAM Footprint](https://img.shields.io/badge/Memory-%3C%2010%20MB-brightgreen)](#performance)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

*Pure C++17 and Win32 APIs. No Electron, no .NET runtime, no background services, and zero telemetry.*

</div>

---

## Overview

**TaskbarMonitor** docks hardware telemetry straight into your Windows taskbar (`Shell_TrayWnd`) using double-buffered GDI rendering. It behaves like an integrated system feature: dynamic width calculation, live theme adaptations, auto-contrast text detection, and seamless transparency.

---

## Real-Time Telemetry

| Metric | Display Format | Data Source | Fallback Mechanism |
| :--- | :--- | :--- | :--- |
| **Network** | `▲ U` (Up) & `▼ D` (Down) | `GetIfTable2` (delta traffic over active interfaces) | Zero baseline if disconnected |
| **Processor** | `CPU` Load % | `GetSystemTimes` kernel/idle/user differentials | System performance counter |
| **CPU Temp** | `C°` Celsius | PDH thermal zones & WMI `MSAcpi_ThermalZoneTemperature` | Smooth dynamic thermal estimate curve |
| **Graphics** | `GPU` Usage % | PDH `\GPU Engine(*)\Utilization Percentage` (summed, max 100%) | `0%` if no GPU engine found |
| **GPU Temp** | `G°` Celsius | NVIDIA NVML (`nvml.dll`), NVAPI, or AMD ADL (`atiadlxx.dll`) | Smooth dynamic thermal estimate curve |
| **RAM** | `RAM %` & `USE` (GB) | `GlobalMemoryStatusEx` (physical committed memory) | Direct Win32 memory struct |
| **Storage** | `DSK %` & Drive Free Space | PDH `PhysicalDisk(_Total)` + `GetDiskFreeSpaceExW` | Target drive letter selectable |
| **Power** | `BAT %` & `⚡` status | `GetSystemPowerStatus` battery / AC state | Automatically hides when on desktop AC |
| **Uptime** | `UP` (hours & mins) | `GetTickCount64` continuous system clock | System tick roll-over safe |

> **Temperature Estimation**: If your motherboard or GPU lacks vendor thermal drivers, TaskbarMonitor falls back to an internal thermal response curve (`38 + cpuUsage * 0.42` / `36 + gpuUsage * 0.38`) to keep readings visible without throwing errors.

---

## Configuration & Settings

Open the configuration window at any time by **double-clicking the tray icon** or choosing **Settings...** from the right-click menu.

### 1. Metrics Tab
- **Two-Column Selection**: Toggle individual hardware sensors on/off independently.
- **Drive Selector**: Choose which drive partition to monitor (`C:\`, `D:\`, etc.).
- **Speed Units**: Toggle between **Bytes/s** (`KB/s`, `MB/s`) and **Bits/s** (`Kbps`, `Mbps`).

### 2. Layout Tab
- **Alignment Modes**: 
  - `Left`: Anchored to the left of the taskbar.
  - `Right (Tray Adjacent)`: Anchored directly next to the notification area / system tray.
  - `Center`: Centered along the taskbar length.
  - `Custom`: Exact manual pixel positioning (`X` and `Y`).
- **Offset & Padding**: Fine-tune horizontal/vertical offsets and column gap spacing.
- **Dividers**: Toggle vertical column divider lines.

### 3. Typography Tab
- **Font Selection**: Choose from installed system fonts (`Segoe UI Variable Display`, `Cascadia Code`, `Consolas`, `Bahnschrift`, etc.) or type custom fonts.
- **Point Size**: Adjustable font scale from 8 pt to 24 pt with automatic DPI scaling.
- **Font Weight**: Normal (400), Medium (500), Semi-Bold (600), or Bold (700).

### 4. Colors Tab
- **Preset Palettes**:
  - `Auto`: Follows Windows accent color and live dark/light mode registry keys.
  - `Dark` / `Light`: Fixed high-contrast palettes.
  - `Custom`: Independent color control for Labels, Values, Upload, Download, Dividers, and Background.
- **Auto-Contrast Engine**: Actively analyzes background luminance ($Y = 0.299R + 0.587G + 0.114B$). Automatically brightens or deepens text colors if contrast drops below readability thresholds ($< 0.42$).
- **Transparent Mode**: Enables color-keyed transparency to blend directly into the taskbar glass.

### 5. Advanced Tab
- **Polling Rate**: Configurable from 100 ms up to 10,000 ms (default: 1000 ms).
- **Settings Theme**: Match system theme, force dark mode, or force light mode.
- **Windows Autostart**: Toggles automatic startup via `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
- **Click-Through Mode**: Makes the overlay window transparent to mouse clicks (`WS_EX_TRANSPARENT`), allowing clicks to pass through to the taskbar below.

---

## Tray Menu Quick Actions

Right-click the **TaskbarMonitor** notification icon in the system tray:

| Action | Function |
| :--- | :--- |
| **Settings...** | Opens the configuration dialog |
| **Open Task Manager** | Spawns `taskmgr.exe` |
| **Refresh Theme** | Re-queries Windows DWM colorization and redraws immediately |
| **Click-Through Mode** | Instantly toggles click pass-through without opening settings |
| **Exit Monitor** | Cleans up hooks, timers, and unloads cleanly |

---

## Architecture & Performance

TaskbarMonitor is built with zero runtime bloat:

- **Double-Buffered GDI**: Eliminates visual flicker during frequent sensor updates.
- **Dynamic LUID Network Tracking**: Correctly monitors VPNs, Wi-Fi, and Ethernet adapters without miscalculating virtual loopbacks.
- **Explorer Shell Hooks**: Listens for `EVENT_OBJECT_LOCATIONCHANGE` and `TaskbarCreated` messages; repositioning itself automatically if the taskbar resizes, restarts, or switches monitors.
- **Runtime Library Loading**: NVIDIA NVML/NVAPI and AMD ADL are loaded dynamically via `LoadLibraryW` only when available. No missing DLL warnings on clean machines.

```
TaskbarMonitor.exe
 ├── Shell Hooks (WinEventHook -> Shell_TrayWnd sync)
 ├── Polling Engine (GetIfTable2, PDH Queries, WMI, GlobalMemoryStatusEx)
 ├── Dynamic Sensors (nvml.dll / nvapi64.dll / atiadlxx.dll)
 └── GDI Double Buffer -> RenderOverlay -> BitBlt -> Taskbar Surface
```

---

## Building from Source

### Prerequisites
- C++17 compliant compiler: **MinGW-w64 (GCC 10+)** or **MSVC (Visual Studio 2019+)**
- Windows SDK (Version 10.0.17763.0 or newer)

### Method 1: Using `build.bat` (Recommended for MinGW)
Compiles source files, links Windows libraries, compiles the resource manifest, and creates a local self-signed code certificate to avoid SmartScreen warnings:
```bat
build.bat
```

### Method 2: Using Makefile
```bash
make
```

### Method 3: Using CMake
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Linked System Libraries
No third-party packages required. Links directly to Windows system libraries:
`iphlpapi` `gdi32` `dwmapi` `pdh` `psapi` `shell32` `comctl32` `comdlg32` `ole32` `oleaut32` `wbemuuid`

---

## Storage & Uninstallation

TaskbarMonitor is fully portable. It writes no files outside its own folder:
- **Configuration**: Stored in `HKCU\Software\TaskbarMonitor`.
- **Autostart Key**: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\TaskbarMonitor`.
- **To uninstall**: Turn off autostart in Settings, exit the application, and delete `TaskbarMonitor.exe`.

---

## License

This project is licensed under the [MIT License](LICENSE).
