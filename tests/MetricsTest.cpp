#include "Config.h"
#include "Metrics.h"
#include "GpuUsage.h"
#include "SettingsWindow.h"
#include "TaskbarSync.h"
#include "Elevation.h"
#include <atomic>
#include <cassert>
#include <cstring>
#include <limits>
#include <cstdio>

// Test the real save implementation without writing the user's Registry.
static LONG createError = ERROR_SUCCESS;
static const wchar_t* failedValue = nullptr;
static int writes = 0, warnings = 0;
static DWORD savedHardware = 0;
static LONG FakeCreateKey(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY key, LPDWORD) {
    writes = 0;
    if (createError == ERROR_SUCCESS) *key = reinterpret_cast<HKEY>(1);
    return createError;
}
static LONG FakeSetValue(HKEY, LPCWSTR name, DWORD, DWORD type, const BYTE* data, DWORD) {
    ++writes;
    if (type == REG_DWORD && wcscmp(name, L"UseLibreHardware") == 0) std::memcpy(&savedHardware, data, sizeof(savedHardware));
    return failedValue && wcscmp(name, failedValue) == 0 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
}
static LONG FakeCloseKey(HKEY key) { return key == reinterpret_cast<HKEY>(1) ? ERROR_SUCCESS : RegCloseKey(key); }
static int FakeMessageBox(HWND, LPCWSTR, LPCWSTR, UINT) { ++warnings; return IDOK; }
#define RegCreateKeyExW FakeCreateKey
#define RegSetValueExW FakeSetValue
#define RegCloseKey FakeCloseKey
#define MessageBoxW FakeMessageBox
#include "../src/Config.cpp"
#undef RegCreateKeyExW
#undef RegSetValueExW
#undef RegCloseKey
#undef MessageBoxW

// Exercise the actual sampler/UI with a deterministic slow backend.
static std::atomic<int> samples{0};
static std::atomic<int> sensorScenario{0};
static HardwareSensorStatus FakeHardwareRead() {
    Sleep(150);
    ++samples;
    HardwareSensorStatus status;
    status.checked = status.running = true;
    status.temperatures.cpu = 61.5;
    status.temperatures.gpu = 53;
    status.cpuSource = L"CPU Package";
    status.gpuSource = L"GPU Core";
    if (sensorScenario == 1) status.temperatures.cpu = 93;
    if (sensorScenario == 2) { status.temperatures.cpu = -1; status.cpuSource.clear(); }
    return status;
}
static void FakeHardwareClose() {}
#define ReadHardwareTemperatures FakeHardwareRead
#define CloseHardwareSensors FakeHardwareClose
#include "../src/Metrics.cpp"
#undef ReadHardwareTemperatures
#undef CloseHardwareSensors

static bool updaterElevated = true;
static int updateResources = 0;
static bool FakeUpdaterElevation() { return updaterElevated; }
static HRSRC FakeUpdateResource(HMODULE, LPCWSTR, LPCWSTR) { ++updateResources; return NULL; }
#define IsProcessElevated FakeUpdaterElevation
#define FindResourceW FakeUpdateResource
#include "../src/Updater.cpp"
#undef IsProcessElevated
#undef FindResourceW

HWND g_hWnd = NULL;
static int elevationRequests = 0;
void SyncWithTaskbar(HWND) {}
static LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_REQUEST_ELEVATION) { ++elevationRequests; return 0; }
    if (message == WM_METRICS_READY) {
        g_metrics = GetLatestMetrics();
        RefreshHardwareSensorStatus();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
template<class Ready> static void WaitFor(Ready ready) {
    ULONGLONG start = GetTickCount64();
    while (!ready()) {
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        assert(GetTickCount64() - start < 15000);
        UpdateAllMetrics();
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
}
static void Click(HWND button) {
    assert(button);
    SendMessageW(GetParent(button), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(button), BN_CLICKED), reinterpret_cast<LPARAM>(button));
}

static RECT ChildRect(HWND window, int id) {
    RECT rectangle;
    assert(GetWindowRect(GetDlgItem(window, id), &rectangle));
    MapWindowPoints(NULL, window, reinterpret_cast<LPPOINT>(&rectangle), 2);
    return rectangle;
}
static void CheckTemperatureLayout(HWND window, UINT dpi) {
    RECT client;
    GetClientRect(window, &client);
    RECT note = ChildRect(window, 1118), notices = ChildRect(window, 1123);
    RECT status = ChildRect(window, 1126), checkbox = ChildRect(window, IDC_CHK_HARDWARE_SENSORS);
    RECT elevate = ChildRect(window, 1128), use = ChildRect(window, 1127);
    assert(note.bottom <= notices.top && notices.bottom < status.top);
    assert(status.bottom < checkbox.top && checkbox.bottom < elevate.top);
    assert(elevate.right < use.left && elevate.top == use.top && elevate.bottom == use.bottom);
    if (use.bottom > client.bottom - MulDiv(80, dpi, 96))
        std::fprintf(stderr, "DPI layout failure: dpi=%u client=%ld use=%ld\n", dpi, client.bottom, use.bottom);
    assert(use.bottom <= client.bottom - MulDiv(80, dpi, 96)); // Clear the footer divider plus a gap.
    for (int id : {1123, 1125, 1127, 1128}) {
        HWND button = GetDlgItem(window, id);
        assert(GetWindowLongPtrW(button, GWL_STYLE) & WS_TABSTOP);
        wchar_t label[128];
        GetWindowTextW(button, label, _countof(label));
        HDC dc = GetDC(button);
        HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0)));
        SIZE size;
        assert(GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &size));
        RECT rectangle = ChildRect(window, id);
        assert(size.cx + MulDiv(12, dpi, 96) < rectangle.right - rectangle.left);
        SelectObject(dc, old);
        ReleaseDC(button, dc);
    }
    for (int id : {1118, 1126}) {
        HWND label = GetDlgItem(window, id);
        wchar_t text[512];
        GetWindowTextW(label, text, _countof(text));
        RECT rectangle;
        GetClientRect(label, &rectangle);
        int height = rectangle.bottom;
        HDC dc = GetDC(label);
        HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(label, WM_GETFONT, 0, 0)));
        DrawTextW(dc, text, -1, &rectangle, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        assert(rectangle.bottom <= height);
        SelectObject(dc, old);
        ReleaseDC(label, dc);
    }
}

int main() {
    SetProcessDPIAware(); // Match the app: compare physical pixels, not virtualized window rectangles.
    SetDefaults();
    CheckForUpdates();
    assert(updateResources == 0); // Elevated sessions must never execute a writable temporary script.
    updaterElevated = false;
    CheckForUpdates();
    assert(updateResources == 1);
    // The EXE must contain and execute the real library, not a downloader/stub.
    HRESULT com = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    assert(SUCCEEDED(com));
    assert(CheckEmbeddedSensorBridge());
    HardwareSensorStatus real = ReadHardwareTemperatures();
    assert(real.checked && real.running && SUCCEEDED(real.error));
    assert(!real.noticesPath.empty() && GetFileAttributesW(real.noticesPath.c_str()) != INVALID_FILE_ATTRIBUTES);
    std::wstring cache = real.noticesPath.substr(0, real.noticesPath.find_last_of(L'\\'));
    HANDLE rename = CreateFileW(cache.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    assert(rename == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);
    HANDLE overwrite = CreateFileW((cache + L"\\TaskbarMonitor.Sensors.dll").c_str(), GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    assert(overwrite == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);

    GpuUsage gpu;
    assert(gpu.Usage() == -1);
    gpu.Add(L"pid_1_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 30);
    gpu.Add(L"pid_2_luid_0x0_0x1_phys_0_eng_0_engtype_3D#1", 25);
    gpu.Add(L"pid_3_luid_0x0_0x1_phys_0_eng_1_engtype_Copy", 50);
    gpu.Add(L"pid_4_luid_0x0_0x2_phys_0_eng_0_engtype_3D", 40);
    gpu.Add(L"invalid", 99);
    gpu.Add(L"pid_5_luid_0x0_0x1_phys_0_eng_0_engtype_3D", std::numeric_limits<double>::quiet_NaN());
    assert(gpu.Usage() == 55);
    gpu.Add(L"pid_6_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 80);
    assert(gpu.Usage() == 100);

    SetDefaults();
    assert(g_config.useLibreHardware && SaveConfig() && warnings == 0);
    int expectedWrites = writes;
    createError = ERROR_ACCESS_DENIED;
    assert(!SaveConfig() && writes == 0 && warnings == 1);
    createError = ERROR_SUCCESS;
    failedValue = L"FontFamily";
    assert(!SaveConfig() && writes == expectedWrites && warnings == 2);
    failedValue = nullptr;
    warnings = 0;

    g_config.showCPUTemp = g_config.showGPUTemp = false;
    HDESK original = GetThreadDesktop(GetCurrentThreadId());
    wchar_t desktopName[64];
    swprintf(desktopName, _countof(desktopName), L"MetricsCheck-%lu", GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(desktopName, NULL, NULL, 0, GENERIC_ALL, NULL);
    assert(desktop && SetThreadDesktop(desktop));
    WNDCLASSW wc = {};
    wc.lpfnWndProc = HostProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MetricsCheckHost";
    assert(RegisterClassW(&wc));
    HWND host = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
    assert(host && InitMetrics(host) && InitMetrics(host));
    OpenSettingsWindow(wc.hInstance, host);
    assert(g_hSettingsWnd);
    Click(FindWindowExW(g_hSettingsWnd, NULL, L"BUTTON", L"Temperatures"));
    HWND refresh = FindWindowExW(g_hSettingsWnd, NULL, L"BUTTON", L"Refresh sensors");
    HWND use = FindWindowExW(g_hSettingsWnd, NULL, L"BUTTON", L"Use detected readings");
    assert(refresh && use && IsWindowVisible(refresh));
    assert(!FindWindowExW(g_hSettingsWnd, NULL, L"BUTTON", L"Download LibreHardwareMonitor"));
    assert(!FindWindowExW(g_hSettingsWnd, NULL, L"EDIT", L"8085"));
    RECT client;
    GetClientRect(g_hSettingsWnd, &client);
    for (HWND child = GetWindow(g_hSettingsWnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!IsWindowVisible(child)) continue;
        RECT bounds; GetWindowRect(child, &bounds);
        MapWindowPoints(NULL, g_hSettingsWnd, reinterpret_cast<POINT*>(&bounds), 2);
        assert(bounds.left >= 0 && bounds.top >= 0 && bounds.right <= client.right && bounds.bottom <= client.bottom);
    }
    ULONGLONG start = GetTickCount64();
    Click(refresh);
    assert(GetTickCount64() - start < 100);
    WaitFor([&] { return GetHardwareSensorStatus().checked; });
    assert(GetHardwareSensorStatus().temperatures.cpu == 61.5 && !g_config.showCPUTemp);
    WaitFor([&] { return IsWindowEnabled(use); });
    failedValue = L"FontFamily";
    Click(use);
    assert(warnings == 1 && !g_config.showCPUTemp && IsWindow(g_hSettingsWnd));
    Click(FindWindowExW(g_hSettingsWnd, NULL, L"BUTTON", L"Save and close"));
    assert(warnings == 2 && IsWindow(g_hSettingsWnd));
    failedValue = nullptr;
    Click(use);
    assert(g_config.useLibreHardware && savedHardware == 1 && g_config.showCPUTemp && g_config.showGPUTemp);
    WaitFor([&] { return GetLatestMetrics().cpuTemp == 61.5; });
    assert(!GetLatestMetrics().cpuTempEstimated && !GetLatestMetrics().gpuTempEstimated);
    ShowTemperatureSettings();
    for (UINT dpi : {96u, 120u, 144u, 192u, 240u}) {
        RECT suggested, client;
        assert(GetWindowRect(g_hSettingsWnd, &suggested) && GetClientRect(g_hSettingsWnd, &client));
        int frameWidth = suggested.right - suggested.left - client.right;
        int frameHeight = suggested.bottom - suggested.top - client.bottom;
        suggested.right = suggested.left + MulDiv(760, dpi, 96) + frameWidth;
        suggested.bottom = suggested.top + MulDiv(560, dpi, 96) + frameHeight;
        SendMessageW(g_hSettingsWnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
        CheckTemperatureLayout(g_hSettingsWnd, dpi);
    }
    sensorScenario = 1;
    assert(RequestHardwareSensorRefresh());
    WaitFor([&] { return GetLatestMetrics().cpuTemp == 93; });
    assert(!GetLatestMetrics().cpuTempEstimated); // Never conceal a real high reading.
    RefreshHardwareSensorStatus();
    wchar_t text[512];
    GetDlgItemTextW(g_hSettingsWnd, 1126, text, _countof(text));
    assert(wcsstr(text, L"CPU Package") && wcsstr(text, L"Verify cooling"));
    CheckTemperatureLayout(g_hSettingsWnd, 240);
    sensorScenario = 2;
    assert(RequestHardwareSensorRefresh());
    WaitFor([&] { return GetHardwareSensorStatus().checked && GetHardwareSensorStatus().temperatures.cpu < 0 && GetLatestMetrics().cpuTempEstimated; });
    assert(GetLatestMetrics().cpuTemp < 90); // Missing CPU data cannot reuse a stale measured 93 C.
    RefreshHardwareSensorStatus();
    GetDlgItemTextW(g_hSettingsWnd, 1126, text, _countof(text));
    assert(wcsstr(text, L"N/A") && wcsstr(text, L"sensor unavailable"));
    HWND elevate = GetDlgItem(g_hSettingsWnd, 1128);
    assert(IsWindowEnabled(elevate) == !IsProcessElevated());
    if (!IsProcessElevated()) {
        int previousWarnings = warnings;
        failedValue = L"UseLibreHardware";
        Click(elevate);
        assert(warnings == previousWarnings + 1 && elevationRequests == 0 && IsWindow(g_hSettingsWnd));
        failedValue = nullptr;
        Click(elevate);
        WaitFor([&] { return elevationRequests == 1; });
        assert(IsWindow(g_hSettingsWnd)); // The button requests a handoff, never kills the UI itself.
    }
    for (int i = 0; i < 32; ++i) assert(RequestHardwareSensorRefresh());
    WaitFor([&] { return GetHardwareSensorStatus().checked; });
    assert(samples > 0);
    CloseSettingsWindow();
    CleanupMetrics(); CleanupMetrics();
    assert(!RequestHardwareSensorRefresh());
    DestroyWindow(host);
    assert(SetThreadDesktop(original));
    CloseDesktop(desktop);
    CloseHardwareSensors();
    CoUninitialize();
}
