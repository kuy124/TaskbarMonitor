# TaskbarMonitor

A hardware monitor that renders directly on the Windows taskbar. Pure C++ and Win32 APIs. No Electron, no .NET, no background services. Uses under 10 MB of RAM.

---

## What It Shows

Each metric updates at your chosen polling rate and renders as a two-row overlay pinned to the taskbar.

| Metric | Data | Source |
|--------|------|--------|
| Network | Upload/download speed (▲/▼) | `GetIfTable2` across all active interfaces |
| CPU | Usage % | `GetSystemTimes` delta |
| CPU Temp | Degrees Celsius | PDH thermal zone, WMI, or synthetic estimate |
| GPU | Usage % | PDH `\GPU Engine` utilization (all engines, capped at 100) |
| GPU Temp | Degrees Celsius | NVML, NVAPI, AMD ADL, or synthetic estimate |
| RAM | Usage % and used GB | `GlobalMemoryStatusEx` |
| Disk | Activity % and free space | PDH disk time + `GetDiskFreeSpaceExW` |
| Processes | Total count | `GetPerformanceInfo` |
| Battery | Charge % | `GetSystemPowerStatus` |
| Uptime | Hours and minutes | `GetTickCount64` |

**Temperature fallbacks**: When no hardware sensor is exposed, CPU and GPU temperatures fall back to synthetic estimates based on load (`38 + cpuUsage * 0.42` for CPU, `36 + gpuUsage * 0.38` for GPU). The display always shows a value, but those numbers come from math, not sensors, on systems without exposed thermal zones.

**Network units**: Switch between Bytes/s (KB/s, MB/s) and Bits/s (Kbps, Mbps) in settings.

---

## Install

1. Grab `TaskbarMonitor.exe` from the [Releases](https://github.com/) page.
2. Drop it anywhere. `C:\Tools\` works. Your Downloads folder works.
3. Run it. It asks whether you want it to start with Windows. Say yes unless you plan to launch it manually each session.

The exe stores all settings in `HKCU\Software\TaskbarMonitor`. No files outside your install folder. No services.

---

## Settings

Double-click the tray icon or right-click and pick **Settings**.

### Metrics Tab

Toggle any metric on or off. Pick which drive to monitor for disk stats. Choose network speed units (Bytes or Bits).

CPU and GPU temperature replace the process count and battery columns when enabled, since the overlay only has so much room.

### Layout Tab

Four alignment modes:

- **Left** (default) -- Pinned from the left edge of the taskbar.
- **Right** -- Sits next to the system tray.
- **Center** -- Centers in the taskbar.
- **Custom** -- Set exact X/Y coordinates within the taskbar.

Adjust horizontal and vertical offsets, spacing between columns, and whether vertical dividers appear between metrics.

### Font Tab

Pick a font family from the dropdown or type your own. Font size ranges from 8 to 24 points. Weight options: Normal, Medium, Semi-Bold, Bold.

### Colors Tab

Four theme presets:

- **Auto** -- Matches your Windows accent color and dark/light mode. Reads `SystemUsesLightTheme` and `ColorPrevalence` from the registry live.
- **Dark** / **Light** -- Fixed palette.
- **Custom** -- Set each color: labels, values, upload, download, dividers, background.

The **auto-contrast engine** adjusts text colors when they blend into the background. It kicks in when luminance difference drops below 0.38 and either brightens or darkens text to keep it readable.

**Transparent Background** removes the solid background so the overlay blends into the taskbar. Disable it if you want a visible background box.

### Advanced Tab

- **Polling Rate**: Minimum 100ms. Default 1000ms. Lower values update faster but use more CPU.
- **Autostart**: Toggle startup registration. The exe writes its path to `HKCU\...\Run` with a `--autostart` flag.
- **Click-Through Mode**: Mouse clicks pass through the overlay to the taskbar beneath. Toggle from the tray menu or this tab.
- **Settings UI Theme**: Force dark or light settings window, or follow Windows. This setting resets on each launch.

---

## Tray Menu

Right-click the tray icon:

| Action | What it does |
|--------|-------------|
| Settings | Opens the configuration window |
| Open Task Manager | Launches `taskmgr.exe` |
| Refresh Theme | Re-reads Windows theme colors and repaints |
| Click-Through Mode | Toggles click-through (saved immediately) |
| Exit Monitor | Closes the application |

Double-clicking the tray icon also opens Settings.

---

## Troubleshooting

**Overlay disappears when I open Start Menu**: It should not. The app hooks Explorer's shell events and re-attaches if the taskbar is recreated. If it does vanish, right-click the tray and pick Refresh Theme. If that fails, restart the exe.

**Temperature shows the same number regardless of load**: Your system does not expose thermal sensors. The synthetic estimate smooths heavily, so it won't jump around like a real sensor. Use a tool like HWMonitor to check whether your motherboard exposes thermal zone data.

**GPU temperature shows 0 or a flat number**: The app tries NVML, then NVAPI (NVIDIA), then AMD ADL. If none load, it falls back to the synthetic estimate. Make sure your GPU vendor's monitoring library is installed.

**Overlay is off-screen after changing resolution or taskbar position**: Right-click the tray and pick Refresh Theme. If that doesn't fix it, open Settings and switch alignment to Left, then adjust the offset.

**Settings dialog didn't ask about autostart**: You launched it with `--autostart`. That flag suppresses the first-run dialog. Remove it from the registry Run key if you want the prompt back, or toggle it from the Advanced tab.

---

## Build from Source

### Requirements

- MinGW-w64 (GCC 10+) or MSVC with Visual Studio
- Windows SDK
- C++17

### Option 1: build.bat

```
build.bat
```

Compiles with g++, then creates a self-signed certificate and signs the exe to suppress "Unknown Publisher" warnings.

### Option 2: Makefile

```
make
```

### Option 3: CMake

```
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Linked Libraries

`iphlpapi` `gdi32` `dwmapi` `pdh` `psapi` `shell32` `comctl32` `comdlg32` `ole32` `oleaut32` `wbemuuid`

All are Windows system libraries. No third-party dependencies.

### Runtime DLLs (loaded dynamically, not linked)

| DLL | Purpose |
|-----|---------|
| `nvml.dll` | NVIDIA GPU temperature |
| `nvapi64.dll` / `nvapi.dll` | NVIDIA GPU temperature (fallback) |
| `atiadlxx.dll` / `atiadlxy.dll` | AMD GPU temperature |

The app loads these at runtime when available. It runs without them but loses hardware temperature readings.

---

## Project Structure

```
TaskbarMonitor/
├── src/
│   ├── Common.h              # Constants, headers, resource IDs
│   ├── Config.h/.cpp         # Registry read/write, autostart
│   ├── Theme.h/.cpp          # Dark/light mode, accent color, contrast
│   ├── Metrics.h/.cpp        # CPU, GPU, RAM, disk, network, power polling
│   ├── Renderer.h/.cpp       # Double-buffered GDI overlay
│   ├── TaskbarSync.h/.cpp    # Position calculation, shell event hooks
│   ├── SettingsWindow.h/.cpp # Win32 settings dialog
│   └── Main.cpp              # Entry point, tray icon, message loop
├── CMakeLists.txt
├── Makefile
└── build.bat
```
