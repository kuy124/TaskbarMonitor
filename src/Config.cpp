#include "Config.h"

MonitorConfig g_config;
int g_curWidth = 430;

#define CONFIG_KEY CONFIG_REGISTRY_KEY
#define RUN_KEY    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define APPROVED_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
#define APP_NAME   L"TaskbarMonitor"

static bool GetCurrentExecutablePath(wchar_t* outPath, DWORD pathCount) {
    DWORD length = GetModuleFileNameW(NULL, outPath, pathCount);
    return length > 0 && length < pathCount;
}

static bool CommandUsesExecutable(const wchar_t* command, const wchar_t* executablePath) {
    if (!command || !executablePath) return false;

    while (*command == L' ' || *command == L'\t') command++;

    const wchar_t* pathStart = command;
    const wchar_t* pathEnd = command;
    if (*command == L'"') {
        pathStart = ++command;
        while (*command && *command != L'"') command++;
        pathEnd = command;
    } else {
        while (*command && *command != L' ' && *command != L'\t') command++;
        pathEnd = command;
    }

    if (pathEnd <= pathStart) return false;

    wchar_t parsedPath[MAX_PATH * 2] = { 0 };
    size_t pathLength = (size_t)(pathEnd - pathStart);
    if (pathLength >= _countof(parsedPath)) return false;
    wcsncpy_s(parsedPath, _countof(parsedPath), pathStart, pathLength);
    return _wcsicmp(parsedPath, executablePath) == 0;
}

struct RunEntryRead {
    bool accessible;
    bool present;
    bool matchesCurrentExecutable;
};

static RunEntryRead ReadRunEntry(const wchar_t* executablePath) {
    RunEntryRead result = { true, false, false };
    HKEY hKey = NULL;
    LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hKey);
    if (openResult == ERROR_FILE_NOT_FOUND) return result;
    if (openResult != ERROR_SUCCESS) {
        result.accessible = false;
        return result;
    }

    wchar_t command[2048] = { 0 };
    DWORD type = 0;
    DWORD size = sizeof(command);
    LONG queryResult = RegQueryValueExW(hKey, APP_NAME, NULL, &type, (LPBYTE)command, &size);
    RegCloseKey(hKey);
    command[_countof(command) - 1] = L'\0';

    if (queryResult == ERROR_FILE_NOT_FOUND) return result;
    if (queryResult != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
        result.accessible = false;
        result.present = true;
        return result;
    }

    wchar_t expandedCommand[2048] = { 0 };
    const wchar_t* commandToCompare = command;
    if (type == REG_EXPAND_SZ) {
        DWORD expandedLength = ExpandEnvironmentStringsW(command, expandedCommand, _countof(expandedCommand));
        if (expandedLength == 0 || expandedLength > _countof(expandedCommand)) {
            result.accessible = false;
            result.present = true;
            return result;
        }
        commandToCompare = expandedCommand;
    }

    result.present = true;
    result.matchesCurrentExecutable = CommandUsesExecutable(commandToCompare, executablePath);
    return result;
}

struct ApprovalEntryRead {
    bool accessible;
    bool present;
    bool valid;
    bool policyManaged;
    BYTE state;
};

static ApprovalEntryRead ReadApprovalEntry() {
    ApprovalEntryRead result = { true, false, false, false, 0 };
    HKEY hKey = NULL;
    LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER, APPROVED_KEY, 0, KEY_READ, &hKey);
    if (openResult == ERROR_FILE_NOT_FOUND) return result;
    if (openResult != ERROR_SUCCESS) {
        result.accessible = false;
        return result;
    }

    BYTE value[32] = { 0 };
    DWORD type = 0;
    DWORD size = sizeof(value);
    LONG queryResult = RegQueryValueExW(hKey, APP_NAME, NULL, &type, value, &size);
    RegCloseKey(hKey);

    if (queryResult == ERROR_FILE_NOT_FOUND) return result;
    result.present = true;
    if (queryResult != ERROR_SUCCESS || type != REG_BINARY || size != 12) return result;

    result.valid = true;
    result.state = value[0];
    result.policyManaged = result.state == 0x08 || result.state == 0x09;
    return result;
}

static bool WriteApprovalState(BYTE state) {
    HKEY hKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APPROVED_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return false;
    }

    BYTE value[12] = { state, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    LONG result = RegSetValueExW(hKey, APP_NAME, 0, REG_BINARY, value, sizeof(value));
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

static bool DeleteApprovalState() {
    HKEY hKey = NULL;
    LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER, APPROVED_KEY, 0, KEY_SET_VALUE, &hKey);
    if (openResult == ERROR_FILE_NOT_FOUND) return true;
    if (openResult != ERROR_SUCCESS) return false;

    LONG deleteResult = RegDeleteValueW(hKey, APP_NAME);
    RegCloseKey(hKey);
    return deleteResult == ERROR_SUCCESS || deleteResult == ERROR_FILE_NOT_FOUND;
}

void SetDefaults() {
    g_config.showNet         = true;
    g_config.showCPU         = true;
    g_config.showGPU         = true;
    g_config.showCPUTemp     = true;
    g_config.showGPUTemp     = true;
    g_config.showRAM         = true;
    g_config.showDisk        = true;
    g_config.showBattery     = true;
    g_config.showUptime      = true;
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
    g_config.netUnit         = NET_UNIT_BITS;
    g_config.clickThrough    = false;
    g_config.runAtStartup    = false;
    g_config.useLocalSensors = false;

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
    g_config.runAtStartup = QueryAutostartStatus().status == AUTOSTART_ENABLED;

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CONFIG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        auto ReadDword = [&](const wchar_t* name, int& target) {
            DWORD val = 0, size = sizeof(DWORD), type = 0;
            if (RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(DWORD)) {
                target = (int)val;
            }
        };
        auto ReadBool = [&](const wchar_t* name, bool& target) {
            DWORD val = 0, size = sizeof(DWORD), type = 0;
            if (RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(DWORD)) {
                target = (val != 0);
            }
        };
        auto ReadColor = [&](const wchar_t* name, COLORREF& target) {
            DWORD val = 0, size = sizeof(DWORD), type = 0;
            if (RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(DWORD)) {
                target = (COLORREF)val;
            }
        };

        ReadBool(L"ShowNet", g_config.showNet);
        ReadBool(L"ShowCPU", g_config.showCPU);
        ReadBool(L"ShowCPUTemp", g_config.showCPUTemp);
        ReadBool(L"ShowGPU", g_config.showGPU);
        ReadBool(L"ShowGPUTemp", g_config.showGPUTemp);
        ReadBool(L"ShowRAM", g_config.showRAM);
        ReadBool(L"ShowDisk", g_config.showDisk);
        ReadBool(L"ShowBattery", g_config.showBattery);
        ReadBool(L"ShowUptime", g_config.showUptime);

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
        ReadBool(L"UseLocalSensors", g_config.useLocalSensors);

        ReadDword(L"ThemeMode", g_config.themeMode);
        ReadBool(L"TransparentBg", g_config.transparentBg);
        ReadBool(L"AutoContrast", g_config.autoContrast);
        ReadColor(L"ColLabel", g_config.colLabel);
        ReadColor(L"ColValue", g_config.colValue);
        ReadColor(L"ColNetUp", g_config.colNetUp);
        ReadColor(L"ColNetDown", g_config.colNetDown);
        ReadColor(L"ColDivider", g_config.colDivider);
        ReadColor(L"ColBg", g_config.colBackground);

        auto ReadString = [&](const wchar_t* name, wchar_t* target, DWORD capacity) {
            DWORD type = 0, size = capacity * sizeof(wchar_t);
            wchar_t buffer[64] = {};
            if (RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)buffer, &size) == ERROR_SUCCESS &&
                type == REG_SZ && size >= sizeof(wchar_t) && size <= capacity * sizeof(wchar_t) &&
                size % sizeof(wchar_t) == 0 && buffer[size / sizeof(wchar_t) - 1] == L'\0') {
                wcscpy_s(target, capacity, buffer);
            }
        };
        ReadString(L"TargetDrive", g_config.targetDrive, 8);
        ReadString(L"FontFamily", g_config.fontFamily, 64);

        RegCloseKey(hKey);
    }
    if (g_config.alignment < ALIGN_LEFT || g_config.alignment > ALIGN_CUSTOM) g_config.alignment = ALIGN_LEFT;
    if (g_config.netUnit < NET_UNIT_BYTES || g_config.netUnit > NET_UNIT_BITS) g_config.netUnit = NET_UNIT_BITS;
    if (g_config.themeMode < THEME_AUTO || g_config.themeMode > THEME_CUSTOM) g_config.themeMode = THEME_AUTO;
    if (g_config.itemSpacing < 0 || g_config.itemSpacing > 100) g_config.itemSpacing = 10;
    if (g_config.fontSize < 1 || g_config.fontSize > 24) g_config.fontSize = 11;
    if (g_config.refreshRateMs < 100 || g_config.refreshRateMs > 10000) g_config.refreshRateMs = 1000;
    if (g_config.targetDrive[0] < L'A' || g_config.targetDrive[0] > L'Z' ||
        wcscmp(g_config.targetDrive + 1, L":\\") != 0) wcscpy_s(g_config.targetDrive, L"C:\\");
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
        WriteBool(L"ShowCPUTemp", g_config.showCPUTemp);
        WriteBool(L"ShowGPU", g_config.showGPU);
        WriteBool(L"ShowGPUTemp", g_config.showGPUTemp);
        WriteBool(L"ShowRAM", g_config.showRAM);
        WriteBool(L"ShowDisk", g_config.showDisk);
        WriteBool(L"ShowBattery", g_config.showBattery);
        WriteBool(L"ShowUptime", g_config.showUptime);

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
        WriteBool(L"UseLocalSensors", g_config.useLocalSensors);

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
}

int CalculateTotalWidth(HDC) {
    int colCount = 0;
    int w = 22;

    int fontScale = g_config.fontSize > 11 ? (g_config.fontSize - 11) * 6 : 0;

    if (g_config.showNet) { 
        w += (96 + fontScale); 
        colCount++; 
    }
    if (g_config.showCPU || g_config.showGPU) { 
        w += (68 + fontScale); 
        colCount++; 
    }
    if (g_config.showCPUTemp || g_config.showGPUTemp) {
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
    return QueryAutostartStatus().status == AUTOSTART_ENABLED;
}

AutostartInfo QueryAutostartStatus() {
    AutostartInfo info = { AUTOSTART_UNAVAILABLE, false, false, false };
    wchar_t exePath[MAX_PATH * 2] = { 0 };
    if (!GetCurrentExecutablePath(exePath, _countof(exePath))) return info;

    RunEntryRead runEntry = ReadRunEntry(exePath);
    if (!runEntry.accessible) return info;
    if (!runEntry.present) {
        info.status = AUTOSTART_NOT_CONFIGURED;
        return info;
    }

    info.hasRunEntry = true;
    if (!runEntry.matchesCurrentExecutable) {
        info.status = AUTOSTART_OTHER_PATH;
        return info;
    }

    ApprovalEntryRead approval = ReadApprovalEntry();
    if (!approval.accessible || (approval.present && !approval.valid)) return info;

    info.hasApprovalEntry = approval.present;
    info.isPolicyManaged = approval.policyManaged;
    if (!approval.present || approval.state == 0x02 || approval.state == 0x06) {
        info.status = AUTOSTART_ENABLED;
    } else if (approval.state == 0x03 || approval.state == 0x07) {
        info.status = AUTOSTART_DISABLED_BY_SYSTEM;
    }
    return info;
}

bool SetAutostart(bool enable) {
    if (enable) {
        wchar_t exePath[MAX_PATH * 2] = { 0 };
        if (!GetCurrentExecutablePath(exePath, _countof(exePath))) return false;

        HKEY hKey = NULL;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
            return false;
        }

        wchar_t command[MAX_PATH * 2] = { 0 };
        swprintf(command, _countof(command), L"\"%ls\" --autostart", exePath);
        DWORD byteCount = (DWORD)((wcslen(command) + 1) * sizeof(wchar_t));
        LONG writeResult = RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (const BYTE*)command, byteCount);
        RegCloseKey(hKey);
        if (writeResult != ERROR_SUCCESS) return false;

        if (!WriteApprovalState(0x02)) return false;
        return QueryAutostartStatus().status == AUTOSTART_ENABLED;
    }

    bool success = true;
    HKEY hKey = NULL;
    LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &hKey);
    if (openResult == ERROR_SUCCESS) {
        LONG deleteResult = RegDeleteValueW(hKey, APP_NAME);
        success = deleteResult == ERROR_SUCCESS || deleteResult == ERROR_FILE_NOT_FOUND;
        RegCloseKey(hKey);
    } else if (openResult != ERROR_FILE_NOT_FOUND) {
        success = false;
    }

    if (!DeleteApprovalState()) success = false;
    return success && QueryAutostartStatus().status == AUTOSTART_NOT_CONFIGURED;
}
