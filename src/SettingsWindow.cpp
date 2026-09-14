#include "SettingsWindow.h"
#include "Config.h"
#include "Theme.h"
#include "Metrics.h"
#include "TaskbarSync.h"

// Tab Button IDs
#define IDC_TAB_BTN_BASE    1200
#define IDC_TAB_0           (IDC_TAB_BTN_BASE + 0) // Metrics
#define IDC_TAB_1           (IDC_TAB_BTN_BASE + 1) // Layout
#define IDC_TAB_2           (IDC_TAB_BTN_BASE + 2) // Typography
#define IDC_TAB_3           (IDC_TAB_BTN_BASE + 3) // Colors and Theme
#define IDC_TAB_4           (IDC_TAB_BTN_BASE + 4) // Advanced

// Label IDs
#define IDC_LBL_DRIVE       1101
#define IDC_LBL_NETUNIT     1102
#define IDC_LBL_ALIGN       1103
#define IDC_LBL_OFFSETX     1104
#define IDC_LBL_OFFSETY     1105
#define IDC_LBL_SPACING     1106
#define IDC_LBL_FONT        1107
#define IDC_LBL_FONTSIZE    1108
#define IDC_LBL_FONTWEIGHT  1109
#define IDC_LBL_THEME       1110
#define IDC_LBL_RATE        1111
#define IDC_LBL_SETTINGSTHEME 1112

// Settings UI Appearance (0: System, 1: Dark, 2: Light)
#define IDC_COMBO_SETTINGSTHEME 1113

HWND g_hSettingsWnd = NULL;
static HWND g_hOwnerWnd = NULL;
static int g_currentTab = 0;
static int g_settingsThemeMode = 0;

static bool s_isDarkMode = true;
static COLORREF s_colWindowBg      = RGB(32, 32, 32);
static COLORREF s_colCardBg        = RGB(44, 44, 44);
static COLORREF s_colControlBg     = RGB(38, 38, 38);
static COLORREF s_colBorder        = RGB(58, 58, 62);
static COLORREF s_colTextPrimary   = RGB(255, 255, 255);
static COLORREF s_colTextSecondary = RGB(160, 160, 160);
static COLORREF s_colAccent        = RGB(96, 205, 255);

static COLORREF s_colLabel, s_colValue, s_colNetUp, s_colNetDown, s_colDivider, s_colBg;

static HFONT hFontTitle = NULL, hFontBody = NULL, hFontBtn = NULL, hFontTab = NULL;
static HBRUSH hBgBrush = NULL, hCardBrush = NULL, hControlBrush = NULL;
static HPEN hCardBorderPen = NULL;

static void UpdateSettingsTheme(HWND hWnd) {
    if (g_settingsThemeMode == 0) { // System
        DWORD isLight = 0, size = sizeof(DWORD);
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExW(hKey, L"SystemUsesLightTheme", NULL, NULL, (LPBYTE)&isLight, &size);
            RegCloseKey(hKey);
        }
        s_isDarkMode = (isLight == 0);
    } else if (g_settingsThemeMode == 1) {
        s_isDarkMode = true;
    } else {
        s_isDarkMode = false;
    }

    DWORD dwmColor = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&dwmColor, &opaque))) {
        BYTE r = (dwmColor >> 16) & 0xFF;
        BYTE g = (dwmColor >> 8) & 0xFF;
        BYTE b = dwmColor & 0xFF;
        s_colAccent = RGB(r, g, b);
    } else {
        s_colAccent = s_isDarkMode ? RGB(96, 205, 255) : RGB(0, 95, 184);
    }

    if (s_isDarkMode) {
        s_colWindowBg      = RGB(32, 32, 32);
        s_colCardBg        = RGB(44, 44, 44);
        s_colControlBg     = RGB(38, 38, 38);
        s_colBorder        = RGB(58, 58, 62);
        s_colTextPrimary   = RGB(255, 255, 255);
        s_colTextSecondary = RGB(160, 160, 160);
    } else {
        s_colWindowBg      = RGB(243, 243, 243);
        s_colCardBg        = RGB(255, 255, 255);
        s_colControlBg     = RGB(238, 238, 240);
        s_colBorder        = RGB(229, 229, 229);
        s_colTextPrimary   = RGB(26, 26, 26);
        s_colTextSecondary = RGB(94, 94, 94);
    }

    BOOL dwmDark = s_isDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dwmDark, sizeof(dwmDark));

    DWORD backdropType = 2;
    DwmSetWindowAttribute(hWnd, 38, &backdropType, sizeof(backdropType));

    if (hBgBrush) DeleteObject(hBgBrush);
    if (hCardBrush) DeleteObject(hCardBrush);
    if (hControlBrush) DeleteObject(hControlBrush);
    if (hCardBorderPen) DeleteObject(hCardBorderPen);

    hBgBrush       = CreateSolidBrush(s_colWindowBg);
    hCardBrush     = CreateSolidBrush(s_colCardBg);
    hControlBrush  = CreateSolidBrush(s_colControlBg);
    hCardBorderPen = CreatePen(PS_SOLID, 1, s_colBorder);

    InvalidateRect(hWnd, NULL, TRUE);
}

static void PickColor(HWND hWnd, COLORREF& targetColor, int btnId) {
    static COLORREF customColors[16] = {0};
    CHOOSECOLORW cc = { sizeof(CHOOSECOLORW) };
    cc.hwndOwner = hWnd;
    cc.lpCustColors = customColors;
    cc.rgbResult = targetColor;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorW(&cc)) {
        targetColor = cc.rgbResult;
        InvalidateRect(GetDlgItem(hWnd, btnId), NULL, TRUE);
    }
}

static void ShowTabControls(HWND hWnd, int tabIndex) {
    g_currentTab = tabIndex;

    const int tab0Controls[] = { 
        IDC_CHK_NET, IDC_CHK_CPU, IDC_CHK_GPU, IDC_CHK_CPUTEMP, IDC_CHK_GPUTEMP, 
        IDC_CHK_RAM, IDC_CHK_DISK, IDC_CHK_BATTERY, IDC_CHK_UPTIME,
        IDC_LBL_DRIVE, IDC_COMBO_DRIVE, IDC_LBL_NETUNIT, IDC_COMBO_NETUNIT, 0 
    };
    const int tab1Controls[] = { 
        IDC_LBL_ALIGN, IDC_COMBO_ALIGN, IDC_LBL_OFFSETX, IDC_EDIT_OFFSETX, 
        IDC_LBL_OFFSETY, IDC_EDIT_OFFSETY, IDC_LBL_SPACING, IDC_EDIT_SPACING, 
        IDC_CHK_DIVIDERS, 0 
    };
    const int tab2Controls[] = { 
        IDC_LBL_FONT, IDC_COMBO_FONT, IDC_LBL_FONTSIZE, IDC_EDIT_FONTSIZE, 
        IDC_LBL_FONTWEIGHT, IDC_COMBO_FONTWEIGHT, 0 
    };
    const int tab3Controls[] = { 
        IDC_LBL_THEME, IDC_COMBO_THEME, IDC_CHK_TRANS_BG, IDC_CHK_AUTOCONTRAST,
        IDC_BTN_COL_LABEL, IDC_BTN_COL_VALUE, IDC_BTN_COL_UP, 
        IDC_BTN_COL_DOWN, IDC_BTN_COL_DIV, IDC_BTN_COL_BG, 0 
    };
    const int tab4Controls[] = { 
        IDC_LBL_RATE, IDC_EDIT_RATE, IDC_CHK_AUTOSTART, IDC_CHK_CLICKTHROUGH,
        IDC_LBL_SETTINGSTHEME, IDC_COMBO_SETTINGSTHEME, 0 
    };

    auto SetControlsVisible = [&](const int* ids, bool visible) {
        for (int i = 0; ids[i] != 0; i++) {
            HWND hCtrl = GetDlgItem(hWnd, ids[i]);
            if (hCtrl) ShowWindow(hCtrl, visible ? SW_SHOW : SW_HIDE);
        }
    };

    SetControlsVisible(tab0Controls, tabIndex == 0);
    SetControlsVisible(tab1Controls, tabIndex == 1);
    SetControlsVisible(tab2Controls, tabIndex == 2);
    SetControlsVisible(tab3Controls, tabIndex == 3);
    SetControlsVisible(tab4Controls, tabIndex == 4);

    for (int i = 0; i < 5; i++) {
        InvalidateRect(GetDlgItem(hWnd, IDC_TAB_BTN_BASE + i), NULL, TRUE);
    }
    InvalidateRect(hWnd, NULL, TRUE);
}

static void ApplyCurrentSettings(HWND hWnd) {
    g_config.showNet         = (IsDlgButtonChecked(hWnd, IDC_CHK_NET) == BST_CHECKED);
    g_config.showCPU         = (IsDlgButtonChecked(hWnd, IDC_CHK_CPU) == BST_CHECKED);
    g_config.showGPU         = (IsDlgButtonChecked(hWnd, IDC_CHK_GPU) == BST_CHECKED);
    g_config.showCPUTemp     = (IsDlgButtonChecked(hWnd, IDC_CHK_CPUTEMP) == BST_CHECKED);
    g_config.showGPUTemp     = (IsDlgButtonChecked(hWnd, IDC_CHK_GPUTEMP) == BST_CHECKED);
    g_config.showRAM         = (IsDlgButtonChecked(hWnd, IDC_CHK_RAM) == BST_CHECKED);
    g_config.showDisk        = (IsDlgButtonChecked(hWnd, IDC_CHK_DISK) == BST_CHECKED);
    g_config.showBattery     = (IsDlgButtonChecked(hWnd, IDC_CHK_BATTERY) == BST_CHECKED);
    g_config.showUptime      = (IsDlgButtonChecked(hWnd, IDC_CHK_UPTIME) == BST_CHECKED);
    g_config.showDividers    = (IsDlgButtonChecked(hWnd, IDC_CHK_DIVIDERS) == BST_CHECKED);
    g_config.transparentBg   = (IsDlgButtonChecked(hWnd, IDC_CHK_TRANS_BG) == BST_CHECKED);
    g_config.autoContrast    = (IsDlgButtonChecked(hWnd, IDC_CHK_AUTOCONTRAST) == BST_CHECKED);
    g_config.runAtStartup    = (IsDlgButtonChecked(hWnd, IDC_CHK_AUTOSTART) == BST_CHECKED);
    g_config.clickThrough    = (IsDlgButtonChecked(hWnd, IDC_CHK_CLICKTHROUGH) == BST_CHECKED);

    HWND hComboDrive = GetDlgItem(hWnd, IDC_COMBO_DRIVE);
    int driveSel = (int)SendMessageW(hComboDrive, CB_GETCURSEL, 0, 0);
    if (driveSel != CB_ERR) SendMessageW(hComboDrive, CB_GETLBTEXT, driveSel, (LPARAM)g_config.targetDrive);

    g_config.netUnit   = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_NETUNIT), CB_GETCURSEL, 0, 0);
    g_config.alignment = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_ALIGN), CB_GETCURSEL, 0, 0);
    g_config.themeMode = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_THEME), CB_GETCURSEL, 0, 0);

    wchar_t editBuf[64];
    GetDlgItemTextW(hWnd, IDC_EDIT_OFFSETX, editBuf, 64);   g_config.offsetX = _wtoi(editBuf);
    GetDlgItemTextW(hWnd, IDC_EDIT_OFFSETY, editBuf, 64);   g_config.offsetY = _wtoi(editBuf);
    GetDlgItemTextW(hWnd, IDC_EDIT_SPACING, editBuf, 64);   g_config.itemSpacing = _wtoi(editBuf);
    GetDlgItemTextW(hWnd, IDC_EDIT_FONTSIZE, editBuf, 64);  g_config.fontSize = _wtoi(editBuf);
    GetDlgItemTextW(hWnd, IDC_EDIT_RATE, editBuf, 64);      g_config.refreshRateMs = _wtoi(editBuf);

    if (g_config.refreshRateMs < 100) g_config.refreshRateMs = 100;
    if (g_config.fontSize < 1)  g_config.fontSize = 1;
    if (g_config.fontSize > 24) g_config.fontSize = 24;

    HWND hComboFont = GetDlgItem(hWnd, IDC_COMBO_FONT);
    int fontSel = (int)SendMessageW(hComboFont, CB_GETCURSEL, 0, 0);
    if (fontSel != CB_ERR) {
        SendMessageW(hComboFont, CB_GETLBTEXT, fontSel, (LPARAM)g_config.fontFamily);
    } else {
        GetDlgItemTextW(hWnd, IDC_COMBO_FONT, g_config.fontFamily, 64);
    }
    if (wcslen(g_config.fontFamily) == 0) {
        wcscpy_s(g_config.fontFamily, L"Segoe UI Variable Display");
    }

    int wSel = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_FONTWEIGHT), CB_GETCURSEL, 0, 0);
    g_config.fontWeight = (wSel == 3) ? FW_BOLD : (wSel == 2 ? FW_SEMIBOLD : (wSel == 1 ? FW_MEDIUM : FW_NORMAL));

    g_config.colLabel      = s_colLabel;
    g_config.colValue      = s_colValue;
    g_config.colNetUp      = s_colNetUp;
    g_config.colNetDown    = s_colNetDown;
    g_config.colDivider    = s_colDivider;
    g_config.colBackground = s_colBg;

    SaveConfig();

    if (g_hOwnerWnd && IsWindow(g_hOwnerWnd)) {
        LONG_PTR exStyle = GetWindowLongPtr(g_hOwnerWnd, GWL_EXSTYLE);
        if (g_config.clickThrough) exStyle |= WS_EX_TRANSPARENT;
        else exStyle &= ~WS_EX_TRANSPARENT;
        SetWindowLongPtr(g_hOwnerWnd, GWL_EXSTYLE, exStyle);

        SetTimer(g_hOwnerWnd, TIMER_METRICS, g_config.refreshRateMs, NULL);
        g_curWidth = CalculateTotalWidth();
        UpdateThemeColors();
        UpdateAllMetrics();
        SyncWithTaskbar(g_hOwnerWnd);
        InvalidateRect(g_hOwnerWnd, NULL, TRUE);
    }
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        DWORD cornerPref = 2;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

        hFontTitle = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        hFontBody = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
        hFontBtn = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
        hFontTab = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

        s_colLabel   = g_config.colLabel;
        s_colValue   = g_config.colValue;
        s_colNetUp   = g_config.colNetUp;
        s_colNetDown = g_config.colNetDown;
        s_colDivider = g_config.colDivider;
        s_colBg      = g_config.colBackground;

        UpdateSettingsTheme(hWnd);

        // Top Navigation Buttons
        const wchar_t* tabTitles[] = { L"Metrics", L"Layout", L"Typography", L"Colors", L"Advanced" };
        int tabW = 94;
        for (int i = 0; i < 5; i++) {
            CreateWindowExW(0, L"BUTTON", tabTitles[i],
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                16 + (i * (tabW + 5)), 12, tabW, 30, hWnd, (HMENU)(UINT_PTR)(IDC_TAB_BTN_BASE + i), NULL, NULL);
        }

        // --- TAB 0: Metrics (Structured Two-Column Grid) ---
        CreateWindowExW(0, L"BUTTON", L"Network Speed (▲/▼)", WS_CHILD | BS_AUTOCHECKBOX, 36, 62, 205, 22, hWnd, (HMENU)IDC_CHK_NET, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Processor (CPU %)", WS_CHILD | BS_AUTOCHECKBOX, 36, 92, 205, 22, hWnd, (HMENU)IDC_CHK_CPU, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Graphics Engine (GPU %)", WS_CHILD | BS_AUTOCHECKBOX, 36, 122, 205, 22, hWnd, (HMENU)IDC_CHK_GPU, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Physical Memory (RAM)", WS_CHILD | BS_AUTOCHECKBOX, 36, 152, 205, 22, hWnd, (HMENU)IDC_CHK_RAM, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Battery & AC Status", WS_CHILD | BS_AUTOCHECKBOX, 36, 182, 205, 22, hWnd, (HMENU)IDC_CHK_BATTERY, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"CPU Temperature (°C)", WS_CHILD | BS_AUTOCHECKBOX, 255, 62, 205, 22, hWnd, (HMENU)IDC_CHK_CPUTEMP, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"GPU Temperature (°C)", WS_CHILD | BS_AUTOCHECKBOX, 255, 92, 205, 22, hWnd, (HMENU)IDC_CHK_GPUTEMP, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Storage Activity & Free", WS_CHILD | BS_AUTOCHECKBOX, 255, 122, 205, 22, hWnd, (HMENU)IDC_CHK_DISK, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"System Uptime", WS_CHILD | BS_AUTOCHECKBOX, 255, 152, 205, 22, hWnd, (HMENU)IDC_CHK_UPTIME, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Storage Target Drive:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 230, 180, 20, hWnd, (HMENU)IDC_LBL_DRIVE, NULL, NULL);
        HWND hComboDrive = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 235, 226, 120, 140, hWnd, (HMENU)IDC_COMBO_DRIVE, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Network Speed Units:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 268, 180, 20, hWnd, (HMENU)IDC_LBL_NETUNIT, NULL, NULL);
        HWND hComboNetU  = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST, 235, 264, 195, 100, hWnd, (HMENU)IDC_COMBO_NETUNIT, NULL, NULL);

        // --- TAB 1: Position and Layout ---
        CreateWindowExW(0, L"STATIC", L"Taskbar Alignment:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 70, 180, 20, hWnd, (HMENU)IDC_LBL_ALIGN, NULL, NULL);
        HWND hComboAlign = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST, 235, 66, 195, 120, hWnd, (HMENU)IDC_COMBO_ALIGN, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Horizontal Offset (px):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 110, 180, 20, hWnd, (HMENU)IDC_LBL_OFFSETX, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"12", WS_CHILD | ES_AUTOHSCROLL, 235, 106, 95, 24, hWnd, (HMENU)IDC_EDIT_OFFSETX, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Vertical Offset (px):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 150, 180, 20, hWnd, (HMENU)IDC_LBL_OFFSETY, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | ES_AUTOHSCROLL, 235, 146, 95, 24, hWnd, (HMENU)IDC_EDIT_OFFSETY, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Item Gap Spacing (px):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 190, 180, 20, hWnd, (HMENU)IDC_LBL_SPACING, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10", WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 235, 186, 95, 24, hWnd, (HMENU)IDC_EDIT_SPACING, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Render Vertical Dividers Between Metrics", WS_CHILD | BS_AUTOCHECKBOX, 36, 238, 420, 22, hWnd, (HMENU)IDC_CHK_DIVIDERS, NULL, NULL);

        // --- TAB 2: Typography ---
        CreateWindowExW(0, L"STATIC", L"Font Family / Name:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 75, 180, 20, hWnd, (HMENU)IDC_LBL_FONT, NULL, NULL);
        HWND hComboFont = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWN | WS_VSCROLL, 235, 71, 215, 220, hWnd, (HMENU)IDC_COMBO_FONT, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Font Size (points):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 120, 180, 20, hWnd, (HMENU)IDC_LBL_FONTSIZE, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"11", WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 235, 116, 95, 24, hWnd, (HMENU)IDC_EDIT_FONTSIZE, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Font Weight:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 165, 180, 20, hWnd, (HMENU)IDC_LBL_FONTWEIGHT, NULL, NULL);
        HWND hComboWeight = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST, 235, 161, 180, 120, hWnd, (HMENU)IDC_COMBO_FONTWEIGHT, NULL, NULL);

        // --- TAB 3: Theme and Colors ---
        CreateWindowExW(0, L"STATIC", L"Monitor Theme Preset:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 66, 180, 20, hWnd, (HMENU)IDC_LBL_THEME, NULL, NULL);
        HWND hComboTheme = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST, 235, 62, 200, 120, hWnd, (HMENU)IDC_COMBO_THEME, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Transparent Background (Seamless Taskbar Blend)", WS_CHILD | BS_AUTOCHECKBOX, 36, 100, 420, 22, hWnd, (HMENU)IDC_CHK_TRANS_BG, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Auto-Adjust Text Contrast for Readability", WS_CHILD | BS_AUTOCHECKBOX, 36, 128, 420, 22, hWnd, (HMENU)IDC_CHK_AUTOCONTRAST, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Labels", WS_CHILD | BS_OWNERDRAW, 36, 166, 200, 32, hWnd, (HMENU)IDC_BTN_COL_LABEL, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Values", WS_CHILD | BS_OWNERDRAW, 250, 166, 200, 32, hWnd, (HMENU)IDC_BTN_COL_VALUE, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Upload (▲)", WS_CHILD | BS_OWNERDRAW, 36, 206, 200, 32, hWnd, (HMENU)IDC_BTN_COL_UP, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Download (▼)", WS_CHILD | BS_OWNERDRAW, 250, 206, 200, 32, hWnd, (HMENU)IDC_BTN_COL_DOWN, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Dividers", WS_CHILD | BS_OWNERDRAW, 36, 246, 200, 32, hWnd, (HMENU)IDC_BTN_COL_DIV, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Background", WS_CHILD | BS_OWNERDRAW, 250, 246, 200, 32, hWnd, (HMENU)IDC_BTN_COL_BG, NULL, NULL);

        // --- TAB 4: Advanced ---
        CreateWindowExW(0, L"STATIC", L"Polling Rate (ms):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 75, 180, 20, hWnd, (HMENU)IDC_LBL_RATE, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1000", WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 235, 71, 105, 24, hWnd, (HMENU)IDC_EDIT_RATE, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Settings UI Theme:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 120, 180, 20, hWnd, (HMENU)IDC_LBL_SETTINGSTHEME, NULL, NULL);
        HWND hComboSettingsTheme = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST, 235, 116, 195, 120, hWnd, (HMENU)IDC_COMBO_SETTINGSTHEME, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Launch automatically on Windows Startup", WS_CHILD | BS_AUTOCHECKBOX, 36, 168, 420, 22, hWnd, (HMENU)IDC_CHK_AUTOSTART, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Click-Through Mode (Clicks pass directly to taskbar)", WS_CHILD | BS_AUTOCHECKBOX, 36, 202, 420, 22, hWnd, (HMENU)IDC_CHK_CLICKTHROUGH, NULL, NULL);

        // Bottom Action Buttons (Flush and Proportional)
        CreateWindowExW(0, L"BUTTON", L"Defaults", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 16, 412, 95, 34, hWnd, (HMENU)IDC_BTN_DEFAULTS, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 198, 412, 88, 34, hWnd, (HMENU)IDC_BTN_APPLY, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 294, 412, 88, 34, hWnd, (HMENU)IDC_BTN_CANCEL, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Save & Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 390, 412, 116, 34, hWnd, (HMENU)IDC_BTN_SAVE, NULL, NULL);

        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, lParam, TRUE);
            return TRUE;
        }, (LPARAM)hFontBody);

        // Load values into controls
        CheckDlgButton(hWnd, IDC_CHK_NET, g_config.showNet ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_CPU, g_config.showCPU ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_GPU, g_config.showGPU ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_CPUTEMP, g_config.showCPUTemp ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_GPUTEMP, g_config.showGPUTemp ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_RAM, g_config.showRAM ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_DISK, g_config.showDisk ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_BATTERY, g_config.showBattery ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_UPTIME, g_config.showUptime ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_DIVIDERS, g_config.showDividers ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_TRANS_BG, g_config.transparentBg ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_AUTOCONTRAST, g_config.autoContrast ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_AUTOSTART, g_config.runAtStartup ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_CLICKTHROUGH, g_config.clickThrough ? BST_CHECKED : BST_UNCHECKED);

        wchar_t driveStrings[512];
        if (GetLogicalDriveStringsW(512, driveStrings)) {
            wchar_t* pDrive = driveStrings;
            int selIdx = 0, curIdx = 0;
            while (*pDrive) {
                SendMessageW(hComboDrive, CB_ADDSTRING, 0, (LPARAM)pDrive);
                if (_wcsicmp(pDrive, g_config.targetDrive) == 0) selIdx = curIdx;
                curIdx++;
                pDrive += wcslen(pDrive) + 1;
            }
            SendMessageW(hComboDrive, CB_SETCURSEL, selIdx, 0);
        }

        SendMessageW(hComboNetU, CB_ADDSTRING, 0, (LPARAM)L"Bytes/s (KB/s, MB/s)");
        SendMessageW(hComboNetU, CB_ADDSTRING, 0, (LPARAM)L"Bits/s (Kbps, Mbps)");
        SendMessageW(hComboNetU, CB_SETCURSEL, g_config.netUnit, 0);

        SendMessageW(hComboAlign, CB_ADDSTRING, 0, (LPARAM)L"Left Aligned");
        SendMessageW(hComboAlign, CB_ADDSTRING, 0, (LPARAM)L"Right (Tray Adjacent)");
        SendMessageW(hComboAlign, CB_ADDSTRING, 0, (LPARAM)L"Center Aligned");
        SendMessageW(hComboAlign, CB_ADDSTRING, 0, (LPARAM)L"Custom Manual Coordinates");
        SendMessageW(hComboAlign, CB_SETCURSEL, g_config.alignment, 0);

        wchar_t numBuf[32];
        swprintf(numBuf, 32, L"%d", g_config.offsetX);       SetDlgItemTextW(hWnd, IDC_EDIT_OFFSETX, numBuf);
        swprintf(numBuf, 32, L"%d", g_config.offsetY);       SetDlgItemTextW(hWnd, IDC_EDIT_OFFSETY, numBuf);
        swprintf(numBuf, 32, L"%d", g_config.itemSpacing);   SetDlgItemTextW(hWnd, IDC_EDIT_SPACING, numBuf);
        swprintf(numBuf, 32, L"%d", g_config.fontSize);      SetDlgItemTextW(hWnd, IDC_EDIT_FONTSIZE, numBuf);
        swprintf(numBuf, 32, L"%d", g_config.refreshRateMs); SetDlgItemTextW(hWnd, IDC_EDIT_RATE, numBuf);

        const wchar_t* popularFonts[] = {
            L"Segoe UI Variable Display", L"Segoe UI Variable Text", L"Segoe UI", 
            L"Cascadia Code", L"Consolas", L"Bahnschrift", L"Calibri", 
            L"Arial", L"Tahoma", L"Lucida Console"
        };
        for (const auto* font : popularFonts) {
            SendMessageW(hComboFont, CB_ADDSTRING, 0, (LPARAM)font);
        }
        int curFontIdx = (int)SendMessageW(hComboFont, CB_FINDSTRINGEXACT, -1, (LPARAM)g_config.fontFamily);
        if (curFontIdx != CB_ERR) {
            SendMessageW(hComboFont, CB_SETCURSEL, curFontIdx, 0);
        } else {
            SetDlgItemTextW(hWnd, IDC_COMBO_FONT, g_config.fontFamily);
        }

        SendMessageW(hComboWeight, CB_ADDSTRING, 0, (LPARAM)L"Normal (400)");
        SendMessageW(hComboWeight, CB_ADDSTRING, 0, (LPARAM)L"Medium (500)");
        SendMessageW(hComboWeight, CB_ADDSTRING, 0, (LPARAM)L"Semi-Bold (600)");
        SendMessageW(hComboWeight, CB_ADDSTRING, 0, (LPARAM)L"Bold (700)");
        int wSel = (g_config.fontWeight == FW_BOLD) ? 3 : (g_config.fontWeight == FW_SEMIBOLD ? 2 : (g_config.fontWeight == FW_MEDIUM ? 1 : 0));
        SendMessageW(hComboWeight, CB_SETCURSEL, wSel, 0);

        SendMessageW(hComboTheme, CB_ADDSTRING, 0, (LPARAM)L"Auto (Windows Accent)");
        SendMessageW(hComboTheme, CB_ADDSTRING, 0, (LPARAM)L"Dark Theme");
        SendMessageW(hComboTheme, CB_ADDSTRING, 0, (LPARAM)L"Light Theme");
        SendMessageW(hComboTheme, CB_ADDSTRING, 0, (LPARAM)L"Fully Custom Palette");
        SendMessageW(hComboTheme, CB_SETCURSEL, g_config.themeMode, 0);

        SendMessageW(hComboSettingsTheme, CB_ADDSTRING, 0, (LPARAM)L"Follow Windows Theme");
        SendMessageW(hComboSettingsTheme, CB_ADDSTRING, 0, (LPARAM)L"Force Dark Mode");
        SendMessageW(hComboSettingsTheme, CB_ADDSTRING, 0, (LPARAM)L"Force Light Mode");
        SendMessageW(hComboSettingsTheme, CB_SETCURSEL, g_settingsThemeMode, 0);

        ShowTabControls(hWnd, 0);
        break;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, hBgBrush);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        HGDIOBJ oldBrush = SelectObject(hdc, hControlBrush);
        HGDIOBJ oldPen   = SelectObject(hdc, hCardBorderPen);
        RoundRect(hdc, 14, 10, 508, 44, 8, 8);

        SelectObject(hdc, hCardBrush);
        RoundRect(hdc, 16, 50, 506, 400, 10, 10);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        SetTextColor(hdcStatic, s_colTextPrimary);
        SetBkColor(hdcStatic, s_colCardBg);
        return (LRESULT)hCardBrush;
    }

    case WM_CTLCOLORBTN: {
        HDC hdcBtn = (HDC)wParam;
        SetBkMode(hdcBtn, TRANSPARENT);
        SetTextColor(hdcBtn, s_colTextPrimary);
        return (LRESULT)hCardBrush;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdcEdit = (HDC)wParam;
        SetTextColor(hdcEdit, s_colTextPrimary);
        SetBkColor(hdcEdit, s_colControlBg);
        return (LRESULT)hControlBrush;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;

        // 1. Navigation Tabs (Windows 11 Segmented Pill Control)
        if (dis->CtlID >= IDC_TAB_0 && dis->CtlID <= IDC_TAB_4) {
            int tabIndex = dis->CtlID - IDC_TAB_BTN_BASE;
            bool isSelected = (g_currentTab == tabIndex);

            HBRUSH trackBg = CreateSolidBrush(s_colControlBg);
            FillRect(dis->hDC, &dis->rcItem, trackBg);
            DeleteObject(trackBg);

            RECT pillRc = dis->rcItem;
            InflateRect(&pillRc, -2, -2);

            if (isSelected) {
                HBRUSH pillBrush = CreateSolidBrush(s_colCardBg);
                HPEN pillPen = CreatePen(PS_SOLID, 1, s_colBorder);
                HGDIOBJ oldPillBrush = SelectObject(dis->hDC, pillBrush);
                HGDIOBJ oldPillPen = SelectObject(dis->hDC, pillPen);
                RoundRect(dis->hDC, pillRc.left, pillRc.top, pillRc.right, pillRc.bottom, 6, 6);
                SelectObject(dis->hDC, oldPillBrush);
                SelectObject(dis->hDC, oldPillPen);
                DeleteObject(pillBrush);
                DeleteObject(pillPen);

                int indW = 16;
                int indX = (pillRc.left + pillRc.right - indW) / 2;
                RECT indRc = { indX, pillRc.bottom - 3, indX + indW, pillRc.bottom };
                HBRUSH indBrush = CreateSolidBrush(s_colAccent);
                HPEN indPen = CreatePen(PS_SOLID, 1, s_colAccent);
                HGDIOBJ oB = SelectObject(dis->hDC, indBrush);
                HGDIOBJ oP = SelectObject(dis->hDC, indPen);
                RoundRect(dis->hDC, indRc.left, indRc.top, indRc.right, indRc.bottom, 2, 2);
                SelectObject(dis->hDC, oB);
                SelectObject(dis->hDC, oP);
                DeleteObject(indBrush);
                DeleteObject(indPen);
            } else if (dis->itemState & ODS_SELECTED) {
                HBRUSH hoverBrush = CreateSolidBrush(s_isDarkMode ? RGB(50, 50, 56) : RGB(230, 230, 235));
                HPEN nullPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
                HGDIOBJ oldHBrush = SelectObject(dis->hDC, hoverBrush);
                HGDIOBJ oldHPen = SelectObject(dis->hDC, nullPen);
                RoundRect(dis->hDC, pillRc.left, pillRc.top, pillRc.right, pillRc.bottom, 6, 6);
                SelectObject(dis->hDC, oldHBrush);
                SelectObject(dis->hDC, oldHPen);
                DeleteObject(hoverBrush);
                DeleteObject(nullPen);
            }

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, isSelected ? s_colTextPrimary : s_colTextSecondary);
            SelectObject(dis->hDC, hFontTab);

            wchar_t tabText[64];
            GetWindowTextW(dis->hwndItem, tabText, 64);
            DrawTextW(dis->hDC, tabText, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return TRUE;
        }

        // 2. Action Buttons (Windows 11 Fluent Button Style)
        if (dis->CtlID == IDC_BTN_SAVE) {
            bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
            COLORREF btnFill = isPressed
                ? RGB((GetRValue(s_colAccent) * 4) / 5, (GetGValue(s_colAccent) * 4) / 5, (GetBValue(s_colAccent) * 4) / 5)
                : s_colAccent;

            HBRUSH btnBrush = CreateSolidBrush(btnFill);
            HPEN btnPen = CreatePen(PS_SOLID, 1, btnFill);
            HGDIOBJ oBrush = SelectObject(dis->hDC, btnBrush);
            HGDIOBJ oPen = SelectObject(dis->hDC, btnPen);
            RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, 8, 8);
            SelectObject(dis->hDC, oBrush);
            SelectObject(dis->hDC, oPen);
            DeleteObject(btnBrush);
            DeleteObject(btnPen);

            double lum = (0.299 * GetRValue(s_colAccent) + 0.587 * GetGValue(s_colAccent) + 0.114 * GetBValue(s_colAccent)) / 255.0;
            COLORREF txtCol = (lum > 0.6) ? RGB(0, 0, 0) : RGB(255, 255, 255);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, txtCol);
            SelectObject(dis->hDC, hFontBtn);
            DrawTextW(dis->hDC, L"Save & Close", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return TRUE;
        } else if (dis->CtlID == IDC_BTN_APPLY || dis->CtlID == IDC_BTN_CANCEL || dis->CtlID == IDC_BTN_DEFAULTS) {
            bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
            COLORREF btnFill = isPressed
                ? (s_isDarkMode ? RGB(56, 56, 62) : RGB(225, 227, 232))
                : (s_isDarkMode ? RGB(44, 44, 48) : RGB(250, 250, 252));

            HBRUSH btnBrush = CreateSolidBrush(btnFill);
            HPEN borderPen = CreatePen(PS_SOLID, 1, s_colBorder);
            HGDIOBJ oBrush = SelectObject(dis->hDC, btnBrush);
            HGDIOBJ oPen = SelectObject(dis->hDC, borderPen);
            RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, 8, 8);
            SelectObject(dis->hDC, oBrush);
            SelectObject(dis->hDC, oPen);
            DeleteObject(btnBrush);
            DeleteObject(borderPen);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, s_colTextPrimary);
            SelectObject(dis->hDC, hFontBtn);
            const wchar_t* btnLbl = (dis->CtlID == IDC_BTN_APPLY) ? L"Apply" : ((dis->CtlID == IDC_BTN_CANCEL) ? L"Cancel" : L"Defaults");
            DrawTextW(dis->hDC, btnLbl, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return TRUE;
        }

        // 3. Custom Color Pickers (Windows 11 Personalization Badges)
        if (dis->CtlID >= IDC_BTN_COL_LABEL && dis->CtlID <= IDC_BTN_COL_BG) {
            COLORREF c = RGB(0, 0, 0);
            const wchar_t* lbl = L"Color";
            switch (dis->CtlID) {
                case IDC_BTN_COL_LABEL: c = s_colLabel; lbl = L"Labels"; break;
                case IDC_BTN_COL_VALUE: c = s_colValue; lbl = L"Values"; break;
                case IDC_BTN_COL_UP:    c = s_colNetUp; lbl = L"Upload (▲)"; break;
                case IDC_BTN_COL_DOWN:  c = s_colNetDown; lbl = L"Download (▼)"; break;
                case IDC_BTN_COL_DIV:   c = s_colDivider; lbl = L"Dividers"; break;
                case IDC_BTN_COL_BG:    c = s_colBg; lbl = L"Background"; break;
            }

            bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
            COLORREF btnFill = isPressed
                ? (s_isDarkMode ? RGB(52, 52, 58) : RGB(228, 230, 236))
                : (s_isDarkMode ? RGB(40, 40, 44) : RGB(255, 255, 255));

            HBRUSH cardBrush = CreateSolidBrush(btnFill);
            HPEN cardBorder = CreatePen(PS_SOLID, 1, s_colBorder);
            HGDIOBJ oB = SelectObject(dis->hDC, cardBrush);
            HGDIOBJ oP = SelectObject(dis->hDC, cardBorder);
            RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, 8, 8);
            SelectObject(dis->hDC, oB);
            SelectObject(dis->hDC, oP);
            DeleteObject(cardBrush);
            DeleteObject(cardBorder);

            int swatchSize = 18;
            int swatchX = dis->rcItem.left + 10;
            int swatchY = dis->rcItem.top + (dis->rcItem.bottom - dis->rcItem.top - swatchSize) / 2;
            HBRUSH swatchBrush = CreateSolidBrush(c);
            HPEN swatchPen = CreatePen(PS_SOLID, 1, s_isDarkMode ? RGB(75, 75, 82) : RGB(190, 192, 200));
            HGDIOBJ oSB = SelectObject(dis->hDC, swatchBrush);
            HGDIOBJ oSP = SelectObject(dis->hDC, swatchPen);
            Ellipse(dis->hDC, swatchX, swatchY, swatchX + swatchSize, swatchY + swatchSize);
            SelectObject(dis->hDC, oSB);
            SelectObject(dis->hDC, oSP);
            DeleteObject(swatchBrush);
            DeleteObject(swatchPen);

            RECT textRc = dis->rcItem;
            textRc.left = swatchX + swatchSize + 10;
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, s_colTextPrimary);
            SelectObject(dis->hDC, hFontBtn);
            DrawTextW(dis->hDC, lbl, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        if (id >= IDC_TAB_0 && id <= IDC_TAB_4) {
            ShowTabControls(hWnd, id - IDC_TAB_BTN_BASE);
            return 0;
        }

        if (id == IDC_COMBO_SETTINGSTHEME && HIWORD(wParam) == CBN_SELCHANGE) {
            g_settingsThemeMode = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_SETTINGSTHEME), CB_GETCURSEL, 0, 0);
            UpdateSettingsTheme(hWnd);
            return 0;
        }

        if (id == IDC_BTN_COL_LABEL) PickColor(hWnd, s_colLabel, IDC_BTN_COL_LABEL);
        else if (id == IDC_BTN_COL_VALUE) PickColor(hWnd, s_colValue, IDC_BTN_COL_VALUE);
        else if (id == IDC_BTN_COL_UP) PickColor(hWnd, s_colNetUp, IDC_BTN_COL_UP);
        else if (id == IDC_BTN_COL_DOWN) PickColor(hWnd, s_colNetDown, IDC_BTN_COL_DOWN);
        else if (id == IDC_BTN_COL_DIV) PickColor(hWnd, s_colDivider, IDC_BTN_COL_DIV);
        else if (id == IDC_BTN_COL_BG) PickColor(hWnd, s_colBg, IDC_BTN_COL_BG);
        else if (id == IDC_BTN_DEFAULTS) {
            SetDefaults();
            DestroyWindow(hWnd);
            OpenSettingsWindow((HINSTANCE)GetWindowLongPtr(g_hOwnerWnd, GWLP_HINSTANCE), g_hOwnerWnd);
        } else if (id == IDC_BTN_APPLY) {
            ApplyCurrentSettings(hWnd);
        } else if (id == IDC_BTN_SAVE) {
            ApplyCurrentSettings(hWnd);
            DestroyWindow(hWnd);
        } else if (id == IDC_BTN_CANCEL) {
            DestroyWindow(hWnd);
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        if (hFontTitle) DeleteObject(hFontTitle);
        if (hFontBody) DeleteObject(hFontBody);
        if (hFontBtn) DeleteObject(hFontBtn);
        if (hFontTab) DeleteObject(hFontTab);
        if (hBgBrush) DeleteObject(hBgBrush);
        if (hCardBrush) DeleteObject(hCardBrush);
        if (hControlBrush) DeleteObject(hControlBrush);
        if (hCardBorderPen) DeleteObject(hCardBorderPen);
        g_hSettingsWnd = NULL;
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void OpenSettingsWindow(HINSTANCE hInstance, HWND hParentWnd) {
    g_hOwnerWnd = hParentWnd;

    if (g_hSettingsWnd && IsWindow(g_hSettingsWnd)) {
        SetForegroundWindow(g_hSettingsWnd);
        return;
    }

    WNDCLASSEXW swc = { sizeof(WNDCLASSEXW) };
    swc.lpfnWndProc = SettingsWndProc;
    swc.hInstance = hInstance;
    swc.lpszClassName = L"TaskbarMonitorSettings";
    swc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&swc);

    int winW = 538;
    int winH = 504;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    g_hSettingsWnd = CreateWindowExW(
        0,
        swc.lpszClassName,
        L"TaskbarMonitor Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        posX, posY, winW, winH,
        NULL, NULL, hInstance, NULL
    );
}

void CloseSettingsWindow() {
    if (g_hSettingsWnd && IsWindow(g_hSettingsWnd)) {
        DestroyWindow(g_hSettingsWnd);
        g_hSettingsWnd = NULL;
    }
}