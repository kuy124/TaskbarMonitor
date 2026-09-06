# TaskbarMonitor

Hardware monitor that runs directly inside the Windows taskbar. Written in C++17 using Win32 APIs and double-buffered GDI. Uses under 10 MB of RAM.

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

If a system lacks exposed thermal sensors, temperature calculations fall back to load-based estimation formulas (38 + CPU * 0.42 and 36 + GPU * 0.38) so the layout does not break.

## Settings

Open the configuration window by double-clicking the tray icon or right-clicking and selecting Settings.

### Metrics
Toggle metrics on or off. Set the target drive letter for storage monitoring and select network speed units (Bytes/s or Bits/s).

### Layout
- Alignment: Left, Right (tray adjacent), Center, or Custom.
- Coordinates: Horizontal offset, vertical offset, item gap spacing.
- Dividers: Toggle vertical lines between metric columns.

### Typography
- Font: Choose any installed Windows font.
- Size: 8 pt to 24 pt.
- Weight: Normal, Medium, Semi-Bold, or Bold.

### Colors
- Presets: Auto (matches Windows accent and dark/light mode), Dark, Light, or Custom.
- Custom colors: Independent color pickers for labels, values, upload, download, dividers, and background.
- Transparent background: Toggles window color-key transparency.
- Auto-contrast: Adjusts font luminosity when background contrast falls below readable thresholds.

### Advanced
- Polling Rate: Sensor refresh interval in milliseconds (minimum 100 ms, default 1000 ms).
- Settings Theme: Follow Windows, Dark Mode, or Light Mode.
- Autostart: Writes the executable path to HKCU Run key.
- Click-Through Mode: Sets WS_EX_TRANSPARENT so mouse clicks pass directly to the taskbar.

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

If none are present, hardware temperatures switch to load-based estimates.

## Configuration & Uninstallation

All settings are stored in the Windows Registry under:
`HKCU\Software\TaskbarMonitor`

Autostart entry:
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run\TaskbarMonitor`

To uninstall, disable autostart in Settings, close the program, and delete the executable. No temporary files or background services are created.

## License

MIT License. See src/License.cpp for details.
