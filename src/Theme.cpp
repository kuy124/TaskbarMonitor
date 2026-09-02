#include "Theme.h"
#include "Config.h"
#include <algorithm>

extern HWND g_hWnd;
COLORREF g_transparentKey = RGB(1, 1, 1);

ThemeColors g_theme = {
    RGB(80, 85, 95),
    RGB(200, 205, 215),
    RGB(255, 255, 255),
    RGB(110, 240, 170),
    RGB(115, 215, 255),
    RGB(25, 25, 28)
};

// Automatic High-Contrast & Readability Engine
static COLORREF AdjustForContrast(COLORREF textCol, COLORREF bgCol) {
    if (!g_config.autoContrast) return textCol;

    double r_bg = (double)GetRValue(bgCol);
    double g_bg = (double)GetGValue(bgCol);
    double b_bg = (double)GetBValue(bgCol);
    double lumBg = (0.299 * r_bg + 0.587 * g_bg + 0.114 * b_bg) / 255.0;

    double r = (double)GetRValue(textCol);
    double g = (double)GetGValue(textCol);
    double b = (double)GetBValue(textCol);
    double lumText = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;

    double contrastDiff = fabs(lumText - lumBg);
    const double minContrast = 0.42;

    if (contrastDiff < minContrast) {
        if (lumBg < 0.5) {
            // Dark Background -> Strongly brighten font colors
            int newR = std::min(255, (int)(r * 1.35 + (255 - r) * 0.5));
            int newG = std::min(255, (int)(g * 1.35 + (255 - g) * 0.5));
            int newB = std::min(255, (int)(b * 1.35 + (255 - b) * 0.5));
            return RGB(newR, newG, newB);
        } else {
            // Light Background -> Strongly darken font colors for crisp contrast
            int newR = std::max(0, (int)(r * 0.35));
            int newG = std::max(0, (int)(g * 0.35));
            int newB = std::max(0, (int)(b * 0.35));
            return RGB(newR, newG, newB);
        }
    }
    return textCol;
}

void UpdateThemeColors() {
    COLORREF referenceBg = g_config.colBackground;

    // Detect taskbar background if in transparent mode
    if (g_config.transparentBg) {
        DWORD isLight = 0, size = sizeof(DWORD);
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExW(hKey, L"SystemUsesLightTheme", NULL, NULL, (LPBYTE)&isLight, &size);
            RegCloseKey(hKey);
        }
        referenceBg = (isLight == 1) ? RGB(242, 242, 246) : RGB(28, 28, 32);
    }

    if (g_config.themeMode == THEME_CUSTOM) {
        COLORREF bg = g_config.transparentBg ? referenceBg : g_config.colBackground;
        g_theme.label      = AdjustForContrast(g_config.colLabel, bg);
        g_theme.value      = AdjustForContrast(g_config.colValue, bg);
        g_theme.upload     = AdjustForContrast(g_config.colNetUp, bg);
        g_theme.download   = AdjustForContrast(g_config.colNetDown, bg);
        g_theme.divider    = AdjustForContrast(g_config.colDivider, bg);
        g_theme.background = g_config.colBackground;
        return;
    }

    if (g_config.themeMode == THEME_LIGHT) {
        COLORREF bg = g_config.transparentBg ? referenceBg : RGB(242, 244, 248);
        g_theme.divider    = RGB(205, 210, 218);
        g_theme.label      = RGB(105, 110, 122);  // Clean muted slate for clear label distinction
        g_theme.value      = RGB(18, 20, 26);     // Solid crisp dark text for values
        g_theme.upload     = RGB(0, 138, 56);     // Vibrant emerald green
        g_theme.download   = RGB(0, 102, 204);    // Vibrant cobalt blue
        g_theme.background = RGB(242, 244, 248);
        return;
    }

    if (g_config.themeMode == THEME_DARK) {
        COLORREF bg = g_config.transparentBg ? referenceBg : RGB(28, 28, 32);
        g_theme.divider    = AdjustForContrast(RGB(75, 75, 82), bg);
        g_theme.label      = AdjustForContrast(RGB(215, 220, 230), bg);
        g_theme.value      = AdjustForContrast(RGB(255, 255, 255), bg);
        g_theme.upload     = AdjustForContrast(RGB(110, 240, 170), bg);
        g_theme.download   = AdjustForContrast(RGB(115, 215, 255), bg);
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

            COLORREF bg = g_config.transparentBg ? referenceBg : (luminance >= 0.65 ? RGB(240, 240, 242) : RGB(24, 24, 28));

            if (luminance >= 0.65) {
                g_theme.divider    = AdjustForContrast(RGB(170, 175, 185), bg);
                g_theme.label      = AdjustForContrast(RGB(40, 45, 55), bg);
                g_theme.value      = AdjustForContrast(RGB(5, 8, 12), bg);
                g_theme.upload     = AdjustForContrast(RGB(0, 110, 40), bg);
                g_theme.download   = AdjustForContrast(RGB(0, 75, 165), bg);
                g_theme.background = RGB(240, 240, 242);
            } else {
                g_theme.divider    = AdjustForContrast(RGB(110, 120, 135), bg);
                g_theme.label      = AdjustForContrast(RGB(225, 232, 245), bg);
                g_theme.value      = AdjustForContrast(RGB(255, 255, 255), bg);
                g_theme.upload     = AdjustForContrast(RGB(120, 255, 185), bg);
                g_theme.download   = AdjustForContrast(RGB(130, 235, 255), bg);
                g_theme.background = RGB(24, 24, 28);
            }
            return;
        }
    }

    bool isLightMode = (g_config.themeMode == THEME_LIGHT) || (g_config.themeMode == THEME_AUTO && isLight == 1);
    g_transparentKey = isLightMode ? RGB(254, 254, 254) : RGB(1, 1, 1);

    if (g_hWnd && IsWindow(g_hWnd)) {
        SetLayeredWindowAttributes(g_hWnd, g_transparentKey, 0, LWA_COLORKEY);
    }

    if (isLightMode) {
        g_theme.divider    = RGB(205, 210, 218);
        g_theme.label      = RGB(105, 110, 122);  // Muted slate gray
        g_theme.value      = RGB(18, 20, 26);     // Crisp near-black text
        g_theme.upload     = RGB(0, 138, 56);     // Emerald green
        g_theme.download   = RGB(0, 102, 204);    // Royal blue
        g_theme.background = RGB(242, 244, 248);
    } else {
        g_theme.divider    = RGB(75, 75, 82);
        g_theme.label      = RGB(215, 220, 230);
        g_theme.value      = RGB(255, 255, 255);
        g_theme.upload     = RGB(110, 240, 170);
        g_theme.download   = RGB(115, 215, 255);
        g_theme.background = RGB(28, 28, 32);
    }
}