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
static int g_settingsThemeMode = 0; // 0 = Follow System, 1 = Dark, 2 = Light

static bool s_isDarkMode = true;
static COLORREF s_colWindowBg      = RGB(28, 28, 30);
static COLORREF s_colCardBg        = RGB(38, 38, 42);
static COLORREF s_colControlBg     = RGB(46, 46, 52);
static COLORREF s_colBorder        = RGB(58, 58, 64);
static COLORREF s_colTextPrimary   = RGB(245, 245, 248);
static COLORREF s_colTextSecondary = RGB(165, 170, 180);
static COLORREF s_colAccent        = RGB(0, 120, 215);

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

    if (s_isDarkMode) {
        s_colWindowBg      = RGB(26, 26, 28);
        s_colCardBg        = RGB(36, 36, 40);
        s_colControlBg     = RGB(46, 46, 52);
        s_colBorder        = RGB(55, 55, 62);
        s_colTextPrimary   = RGB(245, 245, 248);
        s_colTextSecondary = RGB(165, 170, 180);
        s_colAccent        = RGB(0, 122, 220);
    } else {
        s_colWindowBg      = RGB(242, 242, 246);
        s_colCardBg        = RGB(255, 255, 255);
        s_colControlBg     = RGB(245, 245, 248);
        s_colBorder        = RGB(220, 222, 228);
        s_colTextPrimary   = RGB(20, 20, 25);
        s_colTextSecondary = RGB(95, 100, 110);
        s_colAccent        = RGB(0, 105, 195);
    }

    BOOL dwmDark = s_isDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dwmDark, sizeof(dwmDark));

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
        IDC_CHK_NET, IDC_CHK_CPU, IDC_CHK_GPU, IDC_CHK_RAM, IDC_CHK_DISK, 
        IDC_CHK_BATTERY, IDC_CHK_UPTIME, IDC_CHK_PROCESS, 
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
    g_config.showRAM         = (IsDlgButtonChecked(hWnd, IDC_CHK_RAM) == BST_CHECKED);
    g_config.showDisk        = (IsDlgButtonChecked(hWnd, IDC_CHK_DISK) == BST_CHECKED);
    g_config.showBattery     = (IsDlgButtonChecked(hWnd, IDC_CHK_BATTERY) == BST_CHECKED);
    g_config.showUptime      = (IsDlgButtonChecked(hWnd, IDC_CHK_UPTIME) == BST_CHECKED);
    g_config.showProcess     = (IsDlgButtonChecked(hWnd, IDC_CHK_PROCESS) == BST_CHECKED);
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
    if (g_config.fontSize < 8)  g_config.fontSize = 8;
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
        UpdateDisk();
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
        const wchar_t* tabTitles[] = { L"Metrics", L"Layout", L"Font", L"Colors", L"Advanced" };
        int tabW = 88;
        for (int i = 0; i < 5; i++) {
            CreateWindowExW(0, L"BUTTON", tabTitles[i],
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                16 + (i * (tabW + 4)), 12, tabW, 30, hWnd, (HMENU)(UINT_PTR)(IDC_TAB_BTN_BASE + i), NULL, NULL);
        }

        // --- TAB 0: Metrics ---
        CreateWindowExW(0, L"BUTTON", L"Network Speed (Upload / Download)", WS_CHILD | BS_AUTOCHECKBOX, 36, 64, 390, 20, hWnd, (HMENU)IDC_CHK_NET, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Processor (CPU %)", WS_CHILD | BS_AUTOCHECKBOX, 36, 92, 390, 20, hWnd, (HMENU)IDC_CHK_CPU, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Graphics Engine (GPU %)", WS_CHILD | BS_AUTOCHECKBOX, 36, 120, 390, 20, hWnd, (HMENU)IDC_CHK_GPU, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Physical Memory (RAM % and Used GB)", WS_CHILD | BS_AUTOCHECKBOX, 36, 148, 390, 20, hWnd, (HMENU)IDC_CHK_RAM, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Storage Activity and Free Space", WS_CHILD | BS_AUTOCHECKBOX, 36, 176, 390, 20, hWnd, (HMENU)IDC_CHK_DISK, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Battery Percentage (Laptops)", WS_CHILD | BS_AUTOCHECKBOX, 36, 204, 390, 20, hWnd, (HMENU)IDC_CHK_BATTERY, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"System Uptime", WS_CHILD | BS_AUTOCHECKBOX, 36, 232, 390, 20, hWnd, (HMENU)IDC_CHK_UPTIME, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Total Active Process Count", WS_CHILD | BS_AUTOCHECKBOX, 36, 260, 390, 20, hWnd, (HMENU)IDC_CHK_PROCESS, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Storage Target Drive:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 296, 160, 20, hWnd, (HMENU)IDC_LBL_DRIVE, NULL, NULL);
        HWND hComboDrive = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 220, 292, 110, 140, hWnd, (HMENU)IDC_COMBO_DRIVE, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Network Speed Units:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 330, 160, 20, hWnd, (HMENU)IDC_LBL_NETUNIT, NULL, NULL);
        HWND hComboNetU  = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST, 220, 326, 180, 100, hWnd, (HMENU)IDC_COMBO_NETUNIT, NULL, NULL);

        // --- TAB 1: Position and Layout ---
        CreateWindowExW(0, L"STATIC", L"Taskbar Alignment:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 72, 160, 20, hWnd, (HMENU)IDC_LBL_ALIGN, NULL, NULL);
        HWND hComboAlign = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST, 220, 68, 180, 120, hWnd, (HMENU)IDC_COMBO_ALIGN, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Horizontal Offset (px):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 114, 160, 20, hWnd, (HMENU)IDC_LBL_OFFSETX, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"12", WS_CHILD | ES_AUTOHSCROLL, 220, 110, 90, 24, hWnd, (HMENU)IDC_EDIT_OFFSETX, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Vertical Offset (px):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 156, 160, 20, hWnd, (HMENU)IDC_LBL_OFFSETY, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | ES_AUTOHSCROLL, 220, 152, 90, 24, hWnd, (HMENU)IDC_EDIT_OFFSETY, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Item Gap Spacing (px):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 198, 160, 20, hWnd, (HMENU)IDC_LBL_SPACING, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10", WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 220, 194, 90, 24, hWnd, (HMENU)IDC_EDIT_SPACING, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Render Vertical Dividers Between Metrics", WS_CHILD | BS_AUTOCHECKBOX, 36, 244, 390, 20, hWnd, (HMENU)IDC_CHK_DIVIDERS, NULL, NULL);

        // --- TAB 2: Typography ---
        CreateWindowExW(0, L"STATIC", L"Font Family / Name:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 72, 160, 20, hWnd, (HMENU)IDC_LBL_FONT, NULL, NULL);
        HWND hComboFont = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWN | WS_VSCROLL, 220, 68, 200, 220, hWnd, (HMENU)IDC_COMBO_FONT, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Font Size (points):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 118, 160, 20, hWnd, (HMENU)IDC_LBL_FONTSIZE, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"11", WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 220, 114, 90, 24, hWnd, (HMENU)IDC_EDIT_FONTSIZE, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Font Weight:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 164, 160, 20, hWnd, (HMENU)IDC_LBL_FONTWEIGHT, NULL, NULL);
        HWND hComboWeight = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST, 220, 160, 160, 120, hWnd, (HMENU)IDC_COMBO_FONTWEIGHT, NULL, NULL);

        // --- TAB 3: Theme and Colors ---
        CreateWindowExW(0, L"STATIC", L"Monitor Theme Preset:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 68, 160, 20, hWnd, (HMENU)IDC_LBL_THEME, NULL, NULL);
        HWND hComboTheme = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST, 220, 64, 200, 120, hWnd, (HMENU)IDC_COMBO_THEME, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Transparent Background (Seamless Taskbar Blend)", WS_CHILD | BS_AUTOCHECKBOX, 36, 98, 390, 20, hWnd, (HMENU)IDC_CHK_TRANS_BG, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Auto-Adjust Text Contrast for Readability", WS_CHILD | BS_AUTOCHECKBOX, 36, 124, 390, 20, hWnd, (HMENU)IDC_CHK_AUTOCONTRAST, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Labels", WS_CHILD | BS_OWNERDRAW, 36, 154, 185, 30, hWnd, (HMENU)IDC_BTN_COL_LABEL, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Values", WS_CHILD | BS_OWNERDRAW, 235, 154, 185, 30, hWnd, (HMENU)IDC_BTN_COL_VALUE, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Upload (▲)", WS_CHILD | BS_OWNERDRAW, 36, 192, 185, 30, hWnd, (HMENU)IDC_BTN_COL_UP, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Download (▼)", WS_CHILD | BS_OWNERDRAW, 235, 192, 185, 30, hWnd, (HMENU)IDC_BTN_COL_DOWN, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Dividers", WS_CHILD | BS_OWNERDRAW, 36, 230, 185, 30, hWnd, (HMENU)IDC_BTN_COL_DIV, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Background", WS_CHILD | BS_OWNERDRAW, 235, 230, 185, 30, hWnd, (HMENU)IDC_BTN_COL_BG, NULL, NULL);

        // --- TAB 4: Advanced ---
        CreateWindowExW(0, L"STATIC", L"Polling Rate (ms):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 72, 160, 20, hWnd, (HMENU)IDC_LBL_RATE, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1000", WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 220, 68, 100, 24, hWnd, (HMENU)IDC_EDIT_RATE, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Settings UI Theme:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 114, 160, 20, hWnd, (HMENU)IDC_LBL_SETTINGSTHEME, NULL, NULL);
        HWND hComboSettingsTheme = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWNLIST, 220, 110, 180, 120, hWnd, (HMENU)IDC_COMBO_SETTINGSTHEME, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Launch automatically on Windows Startup", WS_CHILD | BS_AUTOCHECKBOX, 36, 160, 390, 20, hWnd, (HMENU)IDC_CHK_AUTOSTART, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Click-Through Mode (Clicks pass directly to taskbar)", WS_CHILD | BS_AUTOCHECKBOX, 36, 192, 390, 20, hWnd, (HMENU)IDC_CHK_CLICKTHROUGH, NULL, NULL);

        // Bottom Action Buttons
        CreateWindowExW(0, L"BUTTON", L"Defaults", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 16, 400, 90, 32, hWnd, (HMENU)IDC_BTN_DEFAULTS, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 172, 400, 84, 32, hWnd, (HMENU)IDC_BTN_APPLY, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Save and Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 264, 400, 114, 32, hWnd, (HMENU)IDC_BTN_SAVE, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 386, 400, 86, 32, hWnd, (HMENU)IDC_BTN_CANCEL, NULL, NULL);

        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, lParam, TRUE);
            return TRUE;
        }, (LPARAM)hFontBody);

        // Load values into controls
        CheckDlgButton(hWnd, IDC_CHK_NET, g_config.showNet ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_CPU, g_config.showCPU ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_GPU, g_config.showGPU ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_RAM, g_config.showRAM ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_DISK, g_config.showDisk ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_BATTERY, g_config.showBattery ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_UPTIME, g_config.showUptime ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_PROCESS, g_config.showProcess ? BST_CHECKED : BST_UNCHECKED);
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

        HGDIOBJ oldBrush = SelectObject(hdc, hCardBrush);
        HGDIOBJ oldPen   = SelectObject(hdc, hCardBorderPen);
        RoundRect(hdc, 16, 48, 474, 386, 10, 10);
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
        
        // 1. Navigation Tabs
        if (dis->CtlID >= IDC_TAB_0 && dis->CtlID <= IDC_TAB_4) {
            int tabIndex = dis->CtlID - IDC_TAB_BTN_BASE;
            bool isSelected = (g_currentTab == tabIndex);

            HBRUSH tabBg = CreateSolidBrush(isSelected ? s_colCardBg : s_colWindowBg);
            FillRect(dis->hDC, &dis->rcItem, tabBg);
            DeleteObject(tabBg);

            if (isSelected) {
                RECT barRc = { dis->rcItem.left + 4, dis->rcItem.bottom - 3, dis->rcItem.right - 4, dis->rcItem.bottom };
                HBRUSH barBrush = CreateSolidBrush(s_colAccent);
                FillRect(dis->hDC, &barRc, barBrush);
                DeleteObject(barBrush);
            }

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, isSelected ? s_colTextPrimary : s_colTextSecondary);
            SelectObject(dis->hDC, hFontTab);

            wchar_t tabText[64];
            GetWindowTextW(dis->hwndItem, tabText, 64);
            DrawTextW(dis->hDC, tabText, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return TRUE;
        }

        // 2. Action Buttons (Save and Close / Apply / Cancel / Defaults)
        if (dis->CtlID == IDC_BTN_SAVE || dis->CtlID == IDC_BTN_APPLY) {
            HBRUSH btnBrush = CreateSolidBrush(dis->itemState & ODS_SELECTED ? RGB(0, 85, 160) : s_colAccent);
            FillRect(dis->hDC, &dis->rcItem, btnBrush);
            DeleteObject(btnBrush);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(255, 255, 255));
            SelectObject(dis->hDC, hFontBtn);
            DrawTextW(dis->hDC, dis->CtlID == IDC_BTN_SAVE ? L"Save and Close" : L"Apply", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return TRUE;
        } else if (dis->CtlID == IDC_BTN_CANCEL || dis->CtlID == IDC_BTN_DEFAULTS) {
            HBRUSH btnBrush = CreateSolidBrush(s_isDarkMode ? (dis->itemState & ODS_SELECTED ? RGB(55, 55, 62) : RGB(42, 42, 48))
                                                            : (dis->itemState & ODS_SELECTED ? RGB(215, 215, 222) : RGB(232, 232, 238)));
            FillRect(dis->hDC, &dis->rcItem, btnBrush);
            DeleteObject(btnBrush);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, s_colTextPrimary);
            SelectObject(dis->hDC, hFontBtn);
            DrawTextW(dis->hDC, dis->CtlID == IDC_BTN_CANCEL ? L"Cancel" : L"Defaults", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return TRUE;
        }

        // 3. Custom Color Pickers
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
            HBRUSH btnBrush = CreateSolidBrush(s_isDarkMode ? RGB(45, 45, 52) : RGB(236, 236, 242));
            FillRect(dis->hDC, &dis->rcItem, btnBrush);
            DeleteObject(btnBrush);

            RECT colorSwatch = { dis->rcItem.left + 8, dis->rcItem.top + 6, dis->rcItem.left + 32, dis->rcItem.bottom - 6 };
            HBRUSH swatchBrush = CreateSolidBrush(c);
            FillRect(dis->hDC, &colorSwatch, swatchBrush);
            DeleteObject(swatchBrush);

            HPEN swatchPen = CreatePen(PS_SOLID, 1, s_colBorder);
            HGDIOBJ op = SelectObject(dis->hDC, swatchPen);
            HGDIOBJ ob = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            Rectangle(dis->hDC, colorSwatch.left, colorSwatch.top, colorSwatch.right, colorSwatch.bottom);
            SelectObject(dis->hDC, op);
            SelectObject(dis->hDC, ob);
            DeleteObject(swatchPen);

            RECT textRc = dis->rcItem;
            textRc.left += 40;
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, s_colTextPrimary);
            SelectObject(dis->hDC, hFontBody);
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
    swc.lpszClassName = L"Windows11NativeTaskbarMonitorSettings";
    swc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&swc);

    int winW = 506;
    int winH = 484;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    g_hSettingsWnd = CreateWindowExW(
        WS_EX_TOPMOST,
        swc.lpszClassName,
        L"Taskbar Monitor Configuration",
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