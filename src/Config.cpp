#include "Config.h"

MonitorConfig g_config;
int g_curWidth = 410;

#define CONFIG_KEY L"Software\\TaskbarMonitor"
#define RUN_KEY    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define APP_NAME   L"TaskbarMonitor"

void SetDefaults() {
    g_config.showNet         = true;
    g_config.showCPU         = true;
    g_config.showGPU         = true;
    g_config.showRAM         = true;
    g_config.showDisk        = true;
    g_config.showBattery     = true;
    g_config.showUptime      = true;
    g_config.showProcess     = true;
    wcscpy_s(g_config.targetDrive, L"C:\\");

    g_config.alignment       = ALIGN_LEFT;
    g_config.offsetX         = 12;
    g_config.offsetY         = 0;
    g_config.itemSpacing     = 10;
    g_config.showDividers    = true;

    wcscpy_s(g_config.fontFamily, L"Segoe UI Variable Display");
    g_config.fontSize        = 11;
    g_config.fontWeight      = FW_SEMIBOLD;

    g_config.refreshRateMs   = 1000;
    g_config.netUnit         = NET_UNIT_BYTES;
    g_config.clickThrough    = false;
    g_config.runAtStartup    = false;

    g_config.themeMode       = THEME_AUTO;
    g_config.transparentBg   = true;
    g_config.autoContrast    = true;
    g_config.colLabel        = RGB(200, 205, 215);
    g_config.colValue        = RGB(255, 255, 255);
    g_config.colNetUp        = RGB(110, 240, 170);
    g_config.colNetDown      = RGB(115, 215, 255);
    g_config.colDivider      = RGB(80, 85, 95);
    g_config.colBackground   = RGB(25, 25, 28);
}

void LoadConfig() {
    SetDefaults();
    g_config.runAtStartup = IsAutostartEnabled();

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CONFIG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        auto ReadDword = [&](const wchar_t* name, int& target) {
            DWORD val = 0, size = sizeof(DWORD);
            if (RegQueryValueExW(hKey, name, NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                target = (int)val;
            }
        };
        auto ReadBool = [&](const wchar_t* name, bool& target) {
            DWORD val = 0, size = sizeof(DWORD);
            if (RegQueryValueExW(hKey, name, NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                target = (val != 0);
            }
        };
        auto ReadColor = [&](const wchar_t* name, COLORREF& target) {
            DWORD val = 0, size = sizeof(DWORD);
            if (RegQueryValueExW(hKey, name, NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                target = (COLORREF)val;
            }
        };

        ReadBool(L"ShowNet", g_config.showNet);
        ReadBool(L"ShowCPU", g_config.showCPU);
        ReadBool(L"ShowGPU", g_config.showGPU);
        ReadBool(L"ShowRAM", g_config.showRAM);
        ReadBool(L"ShowDisk", g_config.showDisk);
        ReadBool(L"ShowBattery", g_config.showBattery);
        ReadBool(L"ShowUptime", g_config.showUptime);
        ReadBool(L"ShowProcess", g_config.showProcess);

        ReadDword(L"Alignment", g_config.alignment);
        ReadDword(L"OffsetX", g_config.offsetX);
        ReadDword(L"OffsetY", g_config.offsetY);
        ReadDword(L"ItemSpacing", g_config.itemSpacing);
        ReadBool(L"ShowDividers", g_config.showDividers);

        ReadDword(L"FontSize", g_config.fontSize);
        ReadDword(L"FontWeight", g_config.fontWeight);
        ReadDword(L"RefreshRate", g_config.refreshRateMs);
        ReadDword(L"NetUnit", g_config.netUnit);
        ReadBool(L"ClickThrough", g_config.clickThrough);

        ReadDword(L"ThemeMode", g_config.themeMode);
        ReadBool(L"TransparentBg", g_config.transparentBg);
        ReadBool(L"AutoContrast", g_config.autoContrast);
        ReadColor(L"ColLabel", g_config.colLabel);
        ReadColor(L"ColValue", g_config.colValue);
        ReadColor(L"ColNetUp", g_config.colNetUp);
        ReadColor(L"ColNetDown", g_config.colNetDown);
        ReadColor(L"ColDivider", g_config.colDivider);
        ReadColor(L"ColBg", g_config.colBackground);

        DWORD size = sizeof(g_config.targetDrive);
        RegQueryValueExW(hKey, L"TargetDrive", NULL, NULL, (LPBYTE)g_config.targetDrive, &size);

        size = sizeof(g_config.fontFamily);
        RegQueryValueExW(hKey, L"FontFamily", NULL, NULL, (LPBYTE)g_config.fontFamily, &size);

        RegCloseKey(hKey);
    }
}

void SaveConfig() {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CONFIG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        auto WriteDword = [&](const wchar_t* name, DWORD val) {
            RegSetValueExW(hKey, name, 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        };
        auto WriteBool = [&](const wchar_t* name, bool val) {
            DWORD d = val ? 1 : 0;
            RegSetValueExW(hKey, name, 0, REG_DWORD, (const BYTE*)&d, sizeof(DWORD));
        };

        WriteBool(L"ShowNet", g_config.showNet);
        WriteBool(L"ShowCPU", g_config.showCPU);
        WriteBool(L"ShowGPU", g_config.showGPU);
        WriteBool(L"ShowRAM", g_config.showRAM);
        WriteBool(L"ShowDisk", g_config.showDisk);
        WriteBool(L"ShowBattery", g_config.showBattery);
        WriteBool(L"ShowUptime", g_config.showUptime);
        WriteBool(L"ShowProcess", g_config.showProcess);

        WriteDword(L"Alignment", (DWORD)g_config.alignment);
        WriteDword(L"OffsetX", (DWORD)g_config.offsetX);
        WriteDword(L"OffsetY", (DWORD)g_config.offsetY);
        WriteDword(L"ItemSpacing", (DWORD)g_config.itemSpacing);
        WriteBool(L"ShowDividers", g_config.showDividers);

        WriteDword(L"FontSize", (DWORD)g_config.fontSize);
        WriteDword(L"FontWeight", (DWORD)g_config.fontWeight);
        WriteDword(L"RefreshRate", (DWORD)g_config.refreshRateMs);
        WriteDword(L"NetUnit", (DWORD)g_config.netUnit);
        WriteBool(L"ClickThrough", g_config.clickThrough);

        WriteDword(L"ThemeMode", (DWORD)g_config.themeMode);
        WriteBool(L"TransparentBg", g_config.transparentBg);
        WriteBool(L"AutoContrast", g_config.autoContrast);
        WriteDword(L"ColLabel", (DWORD)g_config.colLabel);
        WriteDword(L"ColValue", (DWORD)g_config.colValue);
        WriteDword(L"ColNetUp", (DWORD)g_config.colNetUp);
        WriteDword(L"ColNetDown", (DWORD)g_config.colNetDown);
        WriteDword(L"ColDivider", (DWORD)g_config.colDivider);
        WriteDword(L"ColBg", (DWORD)g_config.colBackground);

        RegSetValueExW(hKey, L"TargetDrive", 0, REG_SZ, (const BYTE*)g_config.targetDrive, (DWORD)((wcslen(g_config.targetDrive) + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"FontFamily", 0, REG_SZ, (const BYTE*)g_config.fontFamily, (DWORD)((wcslen(g_config.fontFamily) + 1) * sizeof(wchar_t)));

        RegCloseKey(hKey);
    }
    SetAutostart(g_config.runAtStartup);
}

int CalculateTotalWidth(HDC hdc) {
    int colCount = 0;
    int w = 22;

    int fontScale = g_config.fontSize > 11 ? (g_config.fontSize - 11) * 6 : 0;

    if (g_config.showNet) { 
        w += (88 + fontScale); 
        colCount++; 
    }
    if (g_config.showCPU || g_config.showGPU) { 
        w += (68 + fontScale); 
        colCount++; 
    }
    if (g_config.showRAM) { 
        w += (74 + fontScale); 
        colCount++; 
    }
    if (g_config.showDisk) { 
        w += (76 + fontScale); 
        colCount++; 
    }

    int sysCount = 0;
    if (g_config.showProcess) sysCount++;
    if (g_config.showBattery) sysCount++;
    if (g_config.showUptime)  sysCount++;

    int sysCols = (sysCount + 1) / 2;
    for (int i = 0; i < sysCols; i++) {
        w += (76 + fontScale);
        colCount++;
    }

    if (colCount > 1) {
        w += (colCount - 1) * g_config.itemSpacing;
    }
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