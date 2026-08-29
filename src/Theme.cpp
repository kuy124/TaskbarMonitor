#include "Theme.h"

ThemeColors g_theme = {
    RGB(80, 85, 95),
    RGB(220, 228, 240),
    RGB(255, 255, 255),
    RGB(120, 255, 185),
    RGB(130, 235, 255)
};

void UpdateThemeColors() {
    DWORD isLight = 0;
    DWORD colorPrevalence = 0;
    DWORD size = sizeof(DWORD);
    HKEY hKey;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"SystemUsesLightTheme", NULL, NULL, (LPBYTE)&isLight, &size);
        size = sizeof(DWORD);
        RegQueryValueExW(hKey, L"ColorPrevalence", NULL, NULL, (LPBYTE)&colorPrevalence, &size);
        RegCloseKey(hKey);
    }

    if (colorPrevalence == 1) {
        DWORD dwmColor = 0;
        BOOL opaque = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&dwmColor, &opaque))) {
            BYTE r = (dwmColor >> 16) & 0xFF;
            BYTE g = (dwmColor >> 8) & 0xFF;
            BYTE b = dwmColor & 0xFF;

            double luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;

            if (luminance >= 0.65) {
                g_theme.divider  = RGB(170, 175, 185);
                g_theme.label    = RGB(45, 50, 60);
                g_theme.value    = RGB(10, 15, 20);
                g_theme.upload   = RGB(10, 120, 60);
                g_theme.download = RGB(0, 90, 175);
            } else {
                g_theme.divider  = RGB(110, 120, 135);
                g_theme.label    = RGB(225, 232, 245);
                g_theme.value    = RGB(255, 255, 255);
                g_theme.upload   = RGB(120, 255, 185);
                g_theme.download = RGB(130, 235, 255);
            }
            return;
        }
    }

    if (isLight == 1) {
        g_theme.divider  = RGB(200, 205, 212);
        g_theme.label    = RGB(65, 70, 80);
        g_theme.value    = RGB(15, 18, 22);
        g_theme.upload   = RGB(16, 134, 75);
        g_theme.download = RGB(0, 103, 192);
    } else {
        g_theme.divider  = RGB(75, 75, 82);
        g_theme.label    = RGB(215, 220, 230);
        g_theme.value    = RGB(255, 255, 255);
        g_theme.upload   = RGB(110, 240, 170);
        g_theme.download = RGB(115, 215, 255);
    }
}