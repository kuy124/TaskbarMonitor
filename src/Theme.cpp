#include "Theme.h"
#include "Config.h"

ThemeColors g_theme = {
    RGB(80, 85, 95),
    RGB(200, 205, 215),
    RGB(255, 255, 255),
    RGB(110, 240, 170),
    RGB(115, 215, 255),
    RGB(25, 25, 28)
};

void UpdateThemeColors() {
    if (g_config.themeMode == THEME_CUSTOM) {
        g_theme.label      = g_config.colLabel;
        g_theme.value      = g_config.colValue;
        g_theme.upload     = g_config.colNetUp;
        g_theme.download   = g_config.colNetDown;
        g_theme.divider    = g_config.colDivider;
        g_theme.background = g_config.colBackground;
        return;
    }

    if (g_config.themeMode == THEME_LIGHT) {
        g_theme.divider    = RGB(205, 210, 218);
        g_theme.label      = RGB(70, 75, 85);
        g_theme.value      = RGB(15, 18, 22);
        g_theme.upload     = RGB(16, 134, 75);
        g_theme.download   = RGB(0, 103, 192);
        g_theme.background = RGB(245, 245, 247);
        return;
    }

    if (g_config.themeMode == THEME_DARK) {
        g_theme.divider    = RGB(75, 75, 82);
        g_theme.label      = RGB(215, 220, 230);
        g_theme.value      = RGB(255, 255, 255);
        g_theme.upload     = RGB(110, 240, 170);
        g_theme.download   = RGB(115, 215, 255);
        g_theme.background = RGB(28, 28, 32);
        return;
    }

    // THEME_AUTO: Query Windows Accent and Theme Mode
    DWORD isLight = 0, colorPrevalence = 0, size = sizeof(DWORD);
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
                g_theme.divider    = RGB(170, 175, 185);
                g_theme.label      = RGB(45, 50, 60);
                g_theme.value      = RGB(10, 15, 20);
                g_theme.upload     = RGB(10, 120, 60);
                g_theme.download   = RGB(0, 90, 175);
                g_theme.background = RGB(240, 240, 242);
            } else {
                g_theme.divider    = RGB(110, 120, 135);
                g_theme.label      = RGB(225, 232, 245);
                g_theme.value      = RGB(255, 255, 255);
                g_theme.upload     = RGB(120, 255, 185);
                g_theme.download   = RGB(130, 235, 255);
                g_theme.background = RGB(24, 24, 28);
            }
            return;
        }
    }

    if (isLight == 1) {
        g_theme.divider    = RGB(200, 205, 212);
        g_theme.label      = RGB(65, 70, 80);
        g_theme.value      = RGB(15, 18, 22);
        g_theme.upload     = RGB(16, 134, 75);
        g_theme.download   = RGB(0, 103, 192);
        g_theme.background = RGB(245, 245, 247);
    } else {
        g_theme.divider    = RGB(75, 75, 82);
        g_theme.label      = RGB(215, 220, 230);
        g_theme.value      = RGB(255, 255, 255);
        g_theme.upload     = RGB(110, 240, 170);
        g_theme.download   = RGB(115, 215, 255);
        g_theme.background = RGB(28, 28, 32);
    }
}