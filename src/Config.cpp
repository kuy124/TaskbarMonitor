#include "Config.h"

MonitorConfig g_config = { true, true, true, true, true, L"C:\\", 8, 1000 };
int g_curWidth = 382;

#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define APP_NAME L"TaskbarMonitor"

void LoadConfig() {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\TaskbarMonitor", 0, NULL, 0, KEY_READ, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val = 1, size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"ShowNet", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_config.showNet = (val != 0);
        size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"ShowCompute", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_config.showCompute = (val != 0);
        size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"ShowMemory", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_config.showMemory = (val != 0);
        size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"ShowDisk", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_config.showDisk = (val != 0);
        size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"ShowSystem", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_config.showSystem = (val != 0);
        size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"LeftMargin", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_config.leftMargin = (int)val;
        size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"RefreshRate", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_config.refreshRateMs = (int)val;
        
        size = sizeof(g_config.targetDrive);
        RegQueryValueExW(hKey, L"TargetDrive", NULL, NULL, (LPBYTE)g_config.targetDrive, &size);
        RegCloseKey(hKey);
    }
}

void SaveConfig() {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\TaskbarMonitor", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val;
        val = g_config.showNet ? 1 : 0;      RegSetValueExW(hKey, L"ShowNet", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        val = g_config.showCompute ? 1 : 0;  RegSetValueExW(hKey, L"ShowCompute", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        val = g_config.showMemory ? 1 : 0;   RegSetValueExW(hKey, L"ShowMemory", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        val = g_config.showDisk ? 1 : 0;     RegSetValueExW(hKey, L"ShowDisk", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        val = g_config.showSystem ? 1 : 0;   RegSetValueExW(hKey, L"ShowSystem", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        val = (DWORD)g_config.leftMargin;    RegSetValueExW(hKey, L"LeftMargin", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        val = (DWORD)g_config.refreshRateMs; RegSetValueExW(hKey, L"RefreshRate", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        RegSetValueExW(hKey, L"TargetDrive", 0, REG_SZ, (const BYTE*)g_config.targetDrive, (DWORD)((wcslen(g_config.targetDrive) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

int CalculateTotalWidth() {
    int w = 0;
    int activeCount = 0;
    if (g_config.showNet)     { w += 66; activeCount++; }
    if (g_config.showCompute) { w += 64; activeCount++; }
    if (g_config.showMemory)  { w += 72; activeCount++; }
    if (g_config.showDisk)    { w += 72; activeCount++; }
    if (g_config.showSystem)  { w += 72; activeCount++; }

    if (activeCount > 1) {
        w += (activeCount - 1) * 12;
    }
    w += 14;
    return (w < 40) ? 40 : w;
}

bool IsAutostartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buffer[MAX_PATH * 2] = {0};
        DWORD size = sizeof(buffer);
        LONG res = RegQueryValueExW(hKey, APP_NAME, NULL, NULL, (LPBYTE)buffer, &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

void SetAutostart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exePath[MAX_PATH] = {0};
            GetModuleFileNameW(NULL, exePath, MAX_PATH);

            wchar_t cmd[MAX_PATH * 2] = {0};
            swprintf(cmd, MAX_PATH * 2, L"\"%ls\" --autostart", exePath);

            RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (const BYTE*)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, APP_NAME);
        }
        RegCloseKey(hKey);
    }
}

void CheckAndPromptAutostart(HWND hWndParent) {
    bool isCurrentlyEnabled = IsAutostartEnabled();

    wchar_t promptMsg[300];
    swprintf(
        promptMsg,
        300,
        L"Do you want Taskbar Monitor to start automatically when Windows boots up?\n\n"
        L"Current status: %ls\n\n"
        L"• Click 'Yes' to enable startup with Windows.\n"
        L"• Click 'No' to disable startup.",
        isCurrentlyEnabled ? L"ENABLED" : L"DISABLED"
    );

    int choice = MessageBoxW(
        hWndParent,
        promptMsg,
        L"Taskbar Monitor - Startup Preference",
        MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND
    );

    if (choice == IDYES) {
        SetAutostart(true);
    } else if (choice == IDNO) {
        SetAutostart(false);
    }
}