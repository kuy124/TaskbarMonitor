# TaskbarMonitor

<img src="assets/logo.svg" alt="TaskbarMonitor logo" width="72" height="72">

Hardware monitor that runs directly inside the Windows taskbar. Written in C++17 using Win32 APIs and double-buffered GDI. Uses under 10 MB of RAM.

The logo source is [assets/logo.svg](assets/logo.svg). The Windows icon and PNG are generated with `python tools/generate_icon.py` (requires Pillow and PyMuPDF for asset generation only).

## Metrics

The monitor renders a two-row status block directly onto the taskbar.

| Metric | Display | Data Source |
| :--- | :--- | :--- |
| Network | Upload / Download speed | GetIfTable2 across active non-loopback interfaces |
| CPU Usage | CPU % | GetSystemTimes kernel, idle, and user differentials |
| CPU Temp | Degrees Celsius | PDH thermal zones or WMI MSAcpi_ThermalZoneTemperature |
| GPU Usage | GPU % | PDH \GPU Engine(*)\Utilization Percentage (capped at 100%) |
| GPU Temp | Degrees Celsius | NVIDIA NVML, NVAPI, or AMD ADL |
| Memory | RAM % and Used GB | GlobalMemoryStatusEx |
| Disk | Activity % and Free Space | PDH PhysicalDisk(_Total) and GetDiskFreeSpaceExW |
| Battery | Charge % and AC status | GetSystemPowerStatus (hides automatically on AC-only desktops) |
| Uptime | System uptime | GetTickCount64 |

If a system lacks an exposed thermal sensor, its temperature displays as N/A rather than a load-based estimate. In Advanced settings, optional local LibreHardwareMonitor support can read real CPU and GPU temperatures from its web server at `http://127.0.0.1:8085/data.json` when enabled in that tool. This does not start the server or require the tool for other metrics. LibreHardwareMonitor's server may listen on non-loopback interfaces; check its binding and firewall before enabling it.

## Settings

Open the configuration window by double-clicking the tray icon or right-clicking and selecting Settings.
You can also launch `TaskbarMonitor.exe --settings` to open it directly.

### Metrics
Toggle metrics on or off. Set the target drive letter for storage monitoring and select network speed units (Bytes/s or Bits/s).

### Position
- Alignment: Left, Right (tray adjacent), Center, or Custom.
- Coordinates: Horizontal offset, vertical offset, item gap spacing.
- Dividers: Toggle vertical lines between metric columns.

### Text
- Font: Choose any installed Windows font.
- Size: 1 pt to 24 pt.
- Weight: Normal, Medium, Semi-Bold, or Bold.
- Preview: Shows the selected font with current CPU and memory readings before saving.

### Colors
- Presets: Auto (matches Windows accent and dark/light mode), Dark, Light, or Custom.
- Custom colors: Independent color pickers for labels, values, upload, download, dividers, and background.
- Transparent background: Toggles window color-key transparency.
- Auto-contrast: Adjusts font luminosity when background contrast falls below readable thresholds.

### System
- Polling Rate: Sensor refresh interval in milliseconds (minimum 100 ms, default 1000 ms).
- Settings Theme: Follow Windows, Dark Mode, or Light Mode.
- Autostart: Writes the executable path to the per-user Run key and checks whether Windows Startup Apps has disabled it. The Advanced tab shows the effective state. Enabling the option repairs a Windows-side disable, while unrelated settings changes leave an external disable untouched.
- Click-Through Mode: Sets WS_EX_TRANSPARENT so mouse clicks pass directly to the taskbar.
- Local Sensors: Optional LibreHardwareMonitor temperature source, using the local web endpoint on port 8085.

## Tray Menu

Right-click the notification icon in the system tray:

- Settings: Opens configuration window.
- Open Task Manager: Runs taskmgr.exe.
- Refresh Theme: Re-reads Windows colors and redraws immediately.
- Click-Through Mode: Toggles mouse click pass-through.
- Exit Monitor: Closes the application.

## Build Requirements

- Compiler: MinGW-w64 (GCC 10+) or MSVC (Visual Studio 2019+)
- Windows SDK
- C++17 support

### Build with Batch File (MinGW)
Runs windres, builds TaskbarMonitor.exe with g++, and self-signs the binary:
```bat
build.bat
```

### Build with Make
```bat
make
```

### Build with CMake
```bat
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Linked System Libraries
- iphlpapi
- gdi32
- dwmapi
- pdh
- psapi
- shell32
- comctl32
- comdlg32
- ole32
- oleaut32
- wbemuuid

### Optional Runtime Libraries
These are loaded dynamically via LoadLibraryW when present:
- nvml.dll: NVIDIA Management Library (temperature)
- nvapi64.dll or nvapi.dll: NVIDIA API fallback
- atiadlxx.dll or atiadlxy.dll: AMD Display Library (temperature)

If none are present and the optional local sensor feed is unavailable, temperature displays as N/A.

## Configuration & Uninstallation

All settings are stored in the Windows Registry under:
`HKCU\Software\TaskbarMonitor`

Autostart entry:
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run\TaskbarMonitor`

To uninstall, disable autostart in Settings, close the program, and delete the executable. No temporary files or background services are created.

When the application is launched manually, a startup prompt appears only when Windows startup is not effectively enabled. Launches from the startup entry do not show the prompt. Choosing Keep Disabled leaves the current Windows startup state unchanged.

## License

MIT License. See src/License.cpp for details.
