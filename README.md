# TaskbarMonitor

<img src="assets/logo.svg" alt="TaskbarMonitor logo" width="72" height="72">

Hardware monitor that runs inside the Windows taskbar. Written in C++17 using Win32 APIs and double-buffered GDI, with a small in-process sensor engine for direct and vendor-provided readings.

The logo source is [assets/logo.svg](assets/logo.svg). The Windows icon and PNG are generated with `python tools/generate_icon.py` (requires Pillow and PyMuPDF for asset generation only).

## Automatic updates

TaskbarMonitor checks the public GitHub releases API on startup and every 24 hours.
No GitHub token is needed. When a newer stable version is available, you can choose
whether to download, install, and restart. Disable automatic checks in
**Settings → Advanced → Check for updates automatically**, then click **Apply**.

Updates require a writable application folder. Downloads are checked against
GitHub's SHA-256 release digest and the executable version before replacement.
Failed checks leave the monitor running; failed replacements preserve the old
executable. Releases without a checksum are not installed automatically.

## Screenshots

### Taskbar monitor

![Taskbar monitor showing live network, CPU, GPU, memory, disk, battery, and uptime metrics](screenshots/taskbar.png)

### Settings

![Settings window with controls for visible metrics, appearance, and refresh intervals](screenshots/settings.png)

## Metrics

The monitor renders a two-row status block inside the taskbar.

Network readings show transfer rates, not cumulative totals. Small transfers use B/s or bps so they do not round down to zero; larger rates use KB/s, MB/s, or the corresponding bit units. A full interval with no traffic still shows zero.

| Metric | Display | Data Source |
| :--- | :--- | :--- |
| Network | Upload / Download speed | GetIfTable2 across active non-loopback interfaces |
| CPU Usage | CPU % | GetSystemTimes kernel, idle, and user differentials |
| CPU Temp | Degrees Celsius | Direct CPU sensors, Windows thermal zones, optional monitor telemetry, or explicitly marked estimates |
| GPU Usage | GPU % | PDH \GPU Engine(*)\Utilization Percentage (capped at 100%) |
| GPU Temp | Degrees Celsius | NVIDIA/AMD/Intel GPU APIs, Windows WDDM, external monitor telemetry, or explicitly marked estimates |
| Memory | RAM % and Used GB | GlobalMemoryStatusEx |
| Disk | Activity % and Free Space | PDH PhysicalDisk(_Total) and GetDiskFreeSpaceExW |
| Battery | Charge % and AC status | GetSystemPowerStatus (hides automatically on AC-only desktops) |
| Uptime | System uptime | GetTickCount64 |

If a system lacks an exposed thermal sensor, the monitor estimates temperature based on system load. Estimates are marked with `~` (for example, `~65°C`); measured readings have no prefix. GPU utilization reports the busiest physical engine, combining process counters for that engine rather than summing unrelated engines.

## Built-in temperature readings

On the first interactive launch, TaskbarMonitor asks whether to measure CPU and GPU temperatures. **Yes** starts reading the sensors and keeps the monitor on the taskbar. If a selected reading needs privileged access, Windows asks for administrator permission; the monitor installs the bundled, signed PawnIO driver only if needed. Declining the Windows prompt keeps the current monitor running. **No** uses load-based estimates marked with `~`. **Cancel** uses estimates for this session and asks again next time. Change the choice in **Settings → Temperatures**.

Normal manual launches reuse a saved Yes and can request sensor access again if needed. If a driver is missing on a later launch, the monitor asks before installing it. Windows sign-in launches stay quiet; if you haven't chosen yet, the question appears when you first open Settings.

A sensor engine is embedded in TaskbarMonitor.exe and loaded in-process. It reads CPU and GPU hardware through LibreHardwareMonitor's sensor library, Windows, and graphics driver APIs. There is **no separate monitoring app to install, run, or configure** for the built-in readings.

The page shows detected temperatures and their sensor sources, **Refresh sensors**, and **Use detected readings**. CPU package and physical Tdie readings take priority over core or CCD readings. Generic ACPI/WMI zones are low-priority fallbacks that keep their source label because they can measure an area other than the CPU die. Readings above 90°C remain visible with a cooling warning.

GPU detection checks NVIDIA GeForce and other NVIDIA cards through NVAPI/NVML, AMD Radeon cards and APUs through ADL, and Intel graphics through IGCL, Level Zero Sysman, and the integrated graphics sensor where supported. Windows WDDM and optional telemetry fill gaps. Core readings take priority over hotspot readings. CPU package temperature is never substituted for GPU temperature. On multi-GPU systems the monitor shows one preferred reading.

Memory, VRM, thermal limits, and distance-to-TjMax readings are excluded. A provider that fails to initialize no longer prevents the remaining providers from reporting temperatures.

On supported systems, **Enable sensor access** requests administrator permission to read the CPU and compatible Intel integrated graphics sensors directly. If the signed PawnIO driver is missing, **Install sensor driver** sets it up from inside TaskbarMonitor. An existing compatible driver is used as-is. The current monitor stays running if UAC is cancelled or the restart fails. Use the same Windows account when approving UAC.

Direct temperature support varies by hardware, firmware, and Windows driver access. If a machine doesn't expose a reading, the page shows N/A and taskbar estimates remain marked with `~`. Automatic updates pause in elevated sessions and resume on normal launches.

The .NET Framework 4.7.2+ runtime is required (included on current Windows releases). The embedded sensor libraries and license notice are extracted to `%LOCALAPPDATA%\TaskbarMonitor\Sensors` and checked against the embedded resources before loading.

### Additional temperature sources

The built-in engine and its optional fallbacks cover:

- **NVIDIA NVAPI and NVML:** GPU core temperature from an installed NVIDIA driver.
- **AMD ADL:** Radeon core/edge and junction temperatures through Overdrive and PMLog, depending on the installed driver.
- **Intel IGCL temperature sensors:** standalone ControlLib GPU sensor enumeration.
- **Intel Level Zero Sysman:** GPU-specific sensors when an installed `ze_loader.dll`
  and graphics driver support standalone Sysman.
- **Intel GT cores:** the separate graphics-domain sensor on supported Kaby Lake,
  Coffee Lake, Tiger Lake, and Alder Lake integrated GPUs, using PawnIO with administrator access.
- **HWiNFO shared memory:** enable Shared Memory Support in a running HWiNFO.
  Reads CPU and explicitly labelled GPU / CPU GT Cores temperatures; rejects stale
  data. The free HWiNFO edition limits shared-memory sessions to 12 hours.
- **AIDA64 shared memory:** enable Preferences → Hardware Monitoring → External
  Applications → Enable shared memory in a running AIDA64.
- **LibreHardwareMonitor / OpenHardwareMonitor WMI:** reads an available running
  monitor's CPU/GPU temperature provider.
- **Acer NitroSense telemetry:** subscribes to NitroSense loopback MQTT broker on
  port 15150 while NitroSense's monitoring UI is open. Covers missing CPU and GPU readings, checks message
  freshness, and accepts only the pinned local `CN=localhost` certificate. Set a DWORD
  named `NitroSenseMqttPort` under `HKCU\Software\TaskbarMonitor` if another NitroSense
  build uses a different port.

Settings → Temperatures identifies the selected source. External programs are optional,
not bundled or started automatically. Probe-based fallbacks run every five seconds on the
sensor worker, not the UI thread. NitroSense publishes telemetry only while its
monitoring UI is open, about every ten seconds. Intel APIs require a compatible 64-bit runtime and
driver. Unsupported sensors stay unavailable; CPU package temperature is never used
as GPU temperature. With multiple GPUs, additional fallbacks select the hottest valid
GPU sensor, excluding memory and thermal-limit readings.

Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_temperature_device.ps1 -SensorDirectory build/sensors`
to capture live readings and vendor API availability. Use `-RequireDirectCpu`, `-RequireDirectGpu`, or both to verify that the built-in engine reports direct readings. The Intel UHD graphics in the Lenovo 82KA / Core i3-1115G4 was verified using **Intel GT cores** with administrator access.

## Settings

Open the configuration window by double-clicking the tray icon or right-clicking and selecting Settings.
You can also launch `TaskbarMonitor.exe --settings` to open it directly.

### Metrics
Toggle metrics on or off. Set the target drive letter for storage monitoring and select network speed units (Bytes/s or Bits/s).

### Position
- Alignment: Left, Right (tray adjacent), Center, or Custom.
- Coordinates: The horizontal offset is measured from the taskbar edge. The vertical nudge shifts the readings up or down inside the taskbar band.
- The monitor is a child of the taskbar. It sits in a gap clear of Search, app buttons, and the system tray, and never floats above the desktop. Brief layout-read failures reuse the last valid layout for up to two seconds.
- The monitor fills the taskbar band, so it matches the taskbar height and DPI automatically. Each value sits directly after its label, and the reserved width keeps routine updates from resizing or shifting the layout.
- When auto-hide collapses the taskbar, the monitor slides away with it.
- Item gap spacing adjusts the space between readings.
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

### Temperatures
- Built-in sensor engine, live hardware status, sensor refresh, and one-click use of detected readings. No external sensor setup.
- Settings-save errors are reported without closing the settings window; some registry values may already have been written, so retry after fixing permissions.

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
- .NET Framework 4.7.2+ and its C# compiler (included with the Framework)
- The first build downloads the pinned sensor library and signed driver installer. Subsequent builds can use the cached downloads.

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
ctest --output-on-failure -C Release
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
- advapi32

### Optional Runtime Libraries
These are used when the installed driver provides them:
- nvml.dll: NVIDIA Management Library (GPU temperature)
- atiadlxx.dll: AMD Display Library (GPU temperature)
- ControlLib.dll: Intel Graphics Control Library (GPU temperature)
- ze_loader.dll: Intel Level Zero Sysman (GPU temperature)

If neither the embedded engine nor optional providers give a reading, temperature falls back to a load-based estimate marked with `~`.

## Configuration & Uninstallation

All settings are stored in the Windows Registry under:
`HKCU\Software\TaskbarMonitor`

Autostart entry:
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run\TaskbarMonitor`

To uninstall, disable autostart in Settings, close the program, and delete the executable. The extracted sensor cache can also be deleted from `%LOCALAPPDATA%\TaskbarMonitor\Sensors`. If you enabled the optional PawnIO driver, Windows lists it separately in Installed apps; other hardware tools may also use it.

When the application is launched manually, a startup prompt appears only when Windows startup is not effectively enabled. Launches from the startup entry do not show the prompt. Choosing Keep Disabled leaves the current Windows startup state unchanged.

## Release packaging

Build the release executable, then run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/package_release.ps1 -Executable build-check/TaskbarMonitor.exe`.
The ZIP contains exactly `TaskbarMonitor/TaskbarMonitor.exe` using forward slashes, as required by the 1.2.5 updater. MinGW builds statically link their runtimes so the updater does not need to extract extra DLLs.

## License

TaskbarMonitor is MIT licensed. LibreHardwareMonitorLib is MPL 2.0, and its dependencies retain their upstream licenses. The unmodified PawnIO installer retains its upstream terms; the bundled IntelMCHBAR module is LGPL 2.1 or later. Source links and component notices are in the license dialog and sensor cache.
