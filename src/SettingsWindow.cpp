#include "SettingsWindow.h"
#include "Config.h"
#include "Theme.h"
#include "Metrics.h"
#include "TaskbarSync.h"
#include "Resource.h"

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
#define IDC_LBL_METRIC_NOTE 1114
#define IDC_LBL_LAYOUT_NOTE 1115
#define IDC_LBL_TYPE_NOTE 1116
#define IDC_LBL_COLOR_NOTE 1117
#define IDC_LBL_SENSOR_NOTE 1118
#define IDC_PAGE_TITLE 1119
#define IDC_PAGE_DESCRIPTION 1120

// Settings UI Appearance (0: System, 1: Dark, 2: Light)
#define IDC_COMBO_SETTINGSTHEME 1113

HWND g_hSettingsWnd = NULL;
static HWND g_hOwnerWnd = NULL;
static int g_currentTab = 0;
static int g_settingsThemeMode = 0;
static AutostartStatus s_autostartStatusAtOpen = AUTOSTART_UNAVAILABLE;
static bool s_autostartCheckboxDirty = false;

static bool s_isDarkMode = true;
static COLORREF s_colWindowBg      = RGB(30, 32, 35);
static COLORREF s_colCardBg        = RGB(39, 42, 46);
static COLORREF s_colControlBg     = RGB(48, 52, 57);
static COLORREF s_colBorder        = RGB(65, 70, 76);
static COLORREF s_colTextPrimary   = RGB(255, 255, 255);
static COLORREF s_colTextSecondary = RGB(178, 183, 189);
static COLORREF s_colAccent        = RGB(115, 215, 255);
static UINT s_dpi = 96;

static COLORREF s_colLabel, s_colValue, s_colNetUp, s_colNetDown, s_colDivider, s_colBg;

static HFONT hFontTitle = NULL, hFontBody = NULL, hFontBtn = NULL, hFontTab = NULL;
static HBRUSH hBgBrush = NULL, hCardBrush = NULL, hControlBrush = NULL;
static HPEN hCardBorderPen = NULL;

static int Px(int value) { return MulDiv(value, s_dpi, 96); }

static int ReadSettingsThemeMode() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CONFIG_REGISTRY_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return 0;
    DWORD value = 0, type = 0, size = sizeof(value);
    LONG result = RegQueryValueExW(key, L"SettingsTheme", NULL, &type, (LPBYTE)&value, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(value) && value <= 2
        ? (int)value : 0;
}

static void SaveSettingsThemeMode() {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CONFIG_REGISTRY_KEY, 0, NULL, 0,
                        KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
        DWORD value = (DWORD)g_settingsThemeMode;
        RegSetValueExW(key, L"SettingsTheme", 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
        RegCloseKey(key);
    }
}

static void SetSettingsFonts(HWND hWnd) {
    if (hFontTitle) DeleteObject(hFontTitle);
    if (hFontBody) DeleteObject(hFontBody);
    if (hFontBtn) DeleteObject(hFontBtn);
    if (hFontTab) DeleteObject(hFontTab);
    hFontTitle = CreateFontW(-Px(24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    hFontBody = CreateFontW(-Px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    hFontBtn = CreateFontW(-Px(14), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    hFontTab = CreateFontW(-Px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    EnumChildWindows(hWnd, [](HWND child, LPARAM font) -> BOOL {
        SendMessageW(child, WM_SETFONT, font, TRUE);
        return TRUE;
    }, (LPARAM)hFontBody);
    SendMessageW(GetDlgItem(hWnd, IDC_PAGE_TITLE), WM_SETFONT, (WPARAM)hFontTitle, TRUE);
}

struct ControlPosition { int id, x, y, width, height; };

static void LayoutSettingsControls(HWND hWnd) {
    static const ControlPosition positions[] = {
        { IDC_PAGE_TITLE, 214, 25, 500, 36 },
        { IDC_PAGE_DESCRIPTION, 214, 64, 510, 23 },
        { IDC_TAB_0, 14, 110, 152, 42 }, { IDC_TAB_1, 14, 156, 152, 42 },
        { IDC_TAB_2, 14, 202, 152, 42 }, { IDC_TAB_3, 14, 248, 152, 42 },
        { IDC_TAB_4, 14, 294, 152, 42 },

        { IDC_CHK_NET, 216, 144, 214, 25 }, { IDC_CHK_CPU, 216, 179, 214, 25 },
        { IDC_CHK_GPU, 216, 214, 214, 25 }, { IDC_CHK_RAM, 216, 249, 214, 25 },
        { IDC_CHK_BATTERY, 216, 284, 214, 25 },
        { IDC_CHK_CPUTEMP, 468, 144, 246, 25 }, { IDC_CHK_GPUTEMP, 468, 179, 246, 25 },
        { IDC_CHK_DISK, 468, 214, 246, 25 }, { IDC_CHK_UPTIME, 468, 249, 246, 25 },
        { IDC_LBL_DRIVE, 216, 350, 216, 23 }, { IDC_COMBO_DRIVE, 216, 377, 216, 180 },
        { IDC_LBL_NETUNIT, 468, 350, 246, 23 }, { IDC_COMBO_NETUNIT, 468, 377, 246, 180 },
        { IDC_LBL_METRIC_NOTE, 216, 437, 500, 26 },

        { IDC_LBL_ALIGN, 216, 145, 265, 23 }, { IDC_COMBO_ALIGN, 216, 174, 300, 180 },
        { IDC_LBL_OFFSETX, 216, 241, 145, 23 }, { IDC_EDIT_OFFSETX, 216, 269, 145, 29 },
        { IDC_LBL_OFFSETY, 391, 241, 145, 23 }, { IDC_EDIT_OFFSETY, 391, 269, 145, 29 },
        { IDC_LBL_SPACING, 566, 241, 145, 23 }, { IDC_EDIT_SPACING, 566, 269, 145, 29 },
        { IDC_CHK_DIVIDERS, 216, 349, 450, 26 },
        { IDC_LBL_LAYOUT_NOTE, 216, 404, 500, 26 },

        { IDC_LBL_FONT, 216, 145, 340, 23 }, { IDC_COMBO_FONT, 216, 173, 340, 200 },
        { IDC_LBL_FONTSIZE, 216, 248, 195, 23 }, { IDC_EDIT_FONTSIZE, 216, 277, 145, 29 },
        { IDC_LBL_FONTWEIGHT, 406, 248, 195, 23 }, { IDC_COMBO_FONTWEIGHT, 406, 277, 210, 160 },
        { IDC_LBL_TYPE_NOTE, 216, 337, 500, 26 },

        { IDC_LBL_THEME, 216, 140, 340, 23 }, { IDC_COMBO_THEME, 216, 167, 340, 180 },
        { IDC_CHK_TRANS_BG, 216, 217, 500, 26 }, { IDC_CHK_AUTOCONTRAST, 216, 248, 500, 26 },
        { IDC_LBL_COLOR_NOTE, 216, 282, 500, 22 },
        { IDC_BTN_COL_LABEL, 216, 311, 220, 37 }, { IDC_BTN_COL_VALUE, 468, 311, 220, 37 },
        { IDC_BTN_COL_UP, 216, 356, 220, 37 }, { IDC_BTN_COL_DOWN, 468, 356, 220, 37 },
        { IDC_BTN_COL_DIV, 216, 401, 220, 37 }, { IDC_BTN_COL_BG, 468, 401, 220, 37 },

        { IDC_LBL_RATE, 216, 143, 220, 23 }, { IDC_EDIT_RATE, 216, 172, 170, 29 },
        { IDC_LBL_SETTINGSTHEME, 468, 143, 246, 23 },
        { IDC_COMBO_SETTINGSTHEME, 468, 172, 246, 180 },
        { IDC_CHK_AUTOSTART, 216, 237, 490, 26 }, { IDC_LBL_AUTOSTART_STATUS, 216, 269, 500, 23 },
        { IDC_CHK_CLICKTHROUGH, 216, 330, 490, 26 },
        { IDC_CHK_LOCAL_SENSORS, 216, 383, 490, 26 }, { IDC_LBL_SENSOR_NOTE, 216, 414, 500, 23 },

        { IDC_BTN_DEFAULTS, 214, 510, 157, 35 }, { IDC_BTN_APPLY, 450, 510, 78, 35 },
        { IDC_BTN_CANCEL, 536, 510, 78, 35 }, { IDC_BTN_SAVE, 622, 510, 118, 35 },
    };
    for (const auto& p : positions) {
        HWND control = GetDlgItem(hWnd, p.id);
        if (control) MoveWindow(control, Px(p.x), Px(p.y), Px(p.width), Px(p.height), TRUE);
    }
}

static void DrawFontPreview(HWND hWnd, HDC hdc) {
    if (g_currentTab != 2) return;
    RECT preview = { Px(216), Px(368), Px(688), Px(458) };
    HGDIOBJ oldBrush = SelectObject(hdc, hControlBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hCardBorderPen);
    Rectangle(hdc, preview.left, preview.top, preview.right, preview.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);

    wchar_t family[64] = { 0 };
    GetDlgItemTextW(hWnd, IDC_COMBO_FONT, family, _countof(family));
    if (!family[0]) wcscpy_s(family, g_config.fontFamily);
    UINT points = GetDlgItemInt(hWnd, IDC_EDIT_FONTSIZE, NULL, FALSE);
    if (points < 1) points = 1;
    if (points > 24) points = 24;
    int selection = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_FONTWEIGHT), CB_GETCURSEL, 0, 0);
    int weight = selection == 3 ? FW_BOLD : selection == 2 ? FW_SEMIBOLD :
                 selection == 1 ? FW_MEDIUM : FW_NORMAL;
    HFONT previewFont = CreateFontW(-MulDiv(points, s_dpi, 72), 0, 0, 0, weight,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family);
    HFONT oldFont = (HFONT)SelectObject(hdc, previewFont);
    SetBkMode(hdc, TRANSPARENT);

    wchar_t cpu[32], memory[32];
    swprintf(cpu, _countof(cpu), L"%.0f%%", g_metrics.cpuUsage);
    swprintf(memory, _countof(memory), L"%.0f%%", g_metrics.memUsage);
    const wchar_t* labels[] = { L"CPU", L"RAM" };
    const wchar_t* values[] = { cpu, memory };
    for (int column = 0; column < 2; ++column) {
        int left = Px(column == 0 ? 238 : 452);
        RECT label = { left, Px(377), left + Px(180), Px(415) };
        RECT value = { left, Px(417), left + Px(180), Px(455) };
        SetTextColor(hdc, s_colTextSecondary);
        DrawTextW(hdc, labels[column], -1, &label, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(hdc, s_colTextPrimary);
        DrawTextW(hdc, values[column], -1, &value, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(hdc, oldFont);
    DeleteObject(previewFont);
}

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
        s_colWindowBg      = RGB(30, 32, 35);
        s_colCardBg        = RGB(39, 42, 46);
        s_colControlBg     = RGB(48, 52, 57);
        s_colBorder        = RGB(65, 70, 76);
        s_colTextPrimary   = RGB(245, 247, 249);
        s_colTextSecondary = RGB(178, 183, 189);
        s_colAccent        = RGB(115, 215, 255);
    } else {
        s_colWindowBg      = RGB(250, 251, 252);
        s_colCardBg        = RGB(238, 241, 243);
        s_colControlBg     = RGB(246, 248, 249);
        s_colBorder        = RGB(210, 216, 221);
        s_colTextPrimary   = RGB(27, 35, 41);
        s_colTextSecondary = RGB(83, 96, 106);
        s_colAccent        = RGB(14, 107, 145);
    }

    BOOL dwmDark = s_isDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dwmDark, sizeof(dwmDark));
    const DWORD captionColorAttribute = 35;
    const DWORD captionTextAttribute = 36;
    DwmSetWindowAttribute(hWnd, captionColorAttribute, &s_colCardBg, sizeof(s_colCardBg));
    DwmSetWindowAttribute(hWnd, captionTextAttribute, &s_colTextPrimary, sizeof(s_colTextPrimary));

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

static void RefreshComboThemes(HWND hWnd) {
    const int comboIds[] = { IDC_COMBO_DRIVE, IDC_COMBO_NETUNIT, IDC_COMBO_ALIGN,
                             IDC_COMBO_FONT, IDC_COMBO_FONTWEIGHT, IDC_COMBO_THEME,
                             IDC_COMBO_SETTINGSTHEME };
    for (int id : comboIds) {
        HWND combo = GetDlgItem(hWnd, id);
        if (!combo) continue;
        SendMessageW(combo, WM_THEMECHANGED, 0, 0);
        COMBOBOXINFO info = {};
        info.cbSize = sizeof(info);
        if (GetComboBoxInfo(combo, &info) && info.hwndList)
            SendMessageW(info.hwndList, WM_THEMECHANGED, 0, 0);
        InvalidateRect(combo, NULL, TRUE);
    }
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

    static const wchar_t* titles[] = { L"Metrics", L"Position", L"Text", L"Colors", L"System" };
    static const wchar_t* descriptions[] = {
        L"Choose the readings shown on the taskbar.",
        L"Place the monitor and set the space between readings.",
        L"Set the typeface used for labels and values.",
        L"Choose a preset or edit the monitor palette.",
        L"Control updates, startup, and how the monitor handles clicks."
    };
    SetWindowTextW(GetDlgItem(hWnd, IDC_PAGE_TITLE), titles[tabIndex]);
    SetWindowTextW(GetDlgItem(hWnd, IDC_PAGE_DESCRIPTION), descriptions[tabIndex]);

    const int tab0Controls[] = { 
        IDC_CHK_NET, IDC_CHK_CPU, IDC_CHK_GPU, IDC_CHK_CPUTEMP, IDC_CHK_GPUTEMP, 
        IDC_CHK_RAM, IDC_CHK_DISK, IDC_CHK_BATTERY, IDC_CHK_UPTIME,
        IDC_LBL_DRIVE, IDC_COMBO_DRIVE, IDC_LBL_NETUNIT, IDC_COMBO_NETUNIT,
        IDC_LBL_METRIC_NOTE, 0
    };
    const int tab1Controls[] = { 
        IDC_LBL_ALIGN, IDC_COMBO_ALIGN, IDC_LBL_OFFSETX, IDC_EDIT_OFFSETX, 
        IDC_LBL_OFFSETY, IDC_EDIT_OFFSETY, IDC_LBL_SPACING, IDC_EDIT_SPACING, 
        IDC_CHK_DIVIDERS, IDC_LBL_LAYOUT_NOTE, 0
    };
    const int tab2Controls[] = { 
        IDC_LBL_FONT, IDC_COMBO_FONT, IDC_LBL_FONTSIZE, IDC_EDIT_FONTSIZE, 
        IDC_LBL_FONTWEIGHT, IDC_COMBO_FONTWEIGHT, IDC_LBL_TYPE_NOTE, 0
    };
    const int tab3Controls[] = { 
        IDC_LBL_THEME, IDC_COMBO_THEME, IDC_CHK_TRANS_BG, IDC_CHK_AUTOCONTRAST,
        IDC_BTN_COL_LABEL, IDC_BTN_COL_VALUE, IDC_BTN_COL_UP, 
        IDC_BTN_COL_DOWN, IDC_BTN_COL_DIV, IDC_BTN_COL_BG, IDC_LBL_COLOR_NOTE, 0
    };
    const int tab4Controls[] = { 
        IDC_LBL_RATE, IDC_EDIT_RATE, IDC_CHK_AUTOSTART, IDC_CHK_CLICKTHROUGH,
         IDC_LBL_AUTOSTART_STATUS, IDC_LBL_SETTINGSTHEME, IDC_COMBO_SETTINGSTHEME,
         IDC_CHK_LOCAL_SENSORS, IDC_LBL_SENSOR_NOTE, 0
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

static void UpdateColorPickerAvailability(HWND hWnd) {
    int selected = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_THEME), CB_GETCURSEL, 0, 0);
    bool custom = selected == THEME_CUSTOM;
    for (int id = IDC_BTN_COL_LABEL; id <= IDC_BTN_COL_BG; ++id) {
        EnableWindow(GetDlgItem(hWnd, id), custom);
    }
    SetWindowTextW(GetDlgItem(hWnd, IDC_LBL_COLOR_NOTE),
                   custom ? L"Choose a swatch to set its color."
                          : L"Select Custom to edit individual colors.");
}

static const wchar_t* GetAutostartStatusText(const AutostartInfo& info) {
    switch (info.status) {
    case AUTOSTART_NOT_CONFIGURED:
        return L"Startup status: Not configured";
    case AUTOSTART_ENABLED:
        return L"Startup status: Enabled";
    case AUTOSTART_DISABLED_BY_SYSTEM:
        return L"Startup status: Disabled by Windows";
    case AUTOSTART_OTHER_PATH:
        return L"Startup status: Another copy is configured";
    case AUTOSTART_UNAVAILABLE:
    default:
        if (info.isPolicyManaged) return L"Startup status: Controlled by system policy";
        return L"Startup status: Unable to verify";
    }
}

static void RefreshAutostartControls(HWND hWnd, bool updateCheckboxAndSnapshot) {
    HWND hCheckbox = GetDlgItem(hWnd, IDC_CHK_AUTOSTART);
    HWND hStatus = GetDlgItem(hWnd, IDC_LBL_AUTOSTART_STATUS);
    if (!hCheckbox || !hStatus) return;

    AutostartInfo info = QueryAutostartStatus();
    if (updateCheckboxAndSnapshot && !s_autostartCheckboxDirty) {
        s_autostartStatusAtOpen = info.status;
        CheckDlgButton(hWnd, IDC_CHK_AUTOSTART,
            info.status == AUTOSTART_ENABLED ? BST_CHECKED : BST_UNCHECKED);
    }

    wchar_t statusText[192] = { 0 };
    if (s_autostartCheckboxDirty) {
        swprintf(statusText, _countof(statusText), L"%ls (change pending)", GetAutostartStatusText(info));
    } else {
        wcscpy_s(statusText, _countof(statusText), GetAutostartStatusText(info));
    }
    SetWindowTextW(hStatus, statusText);
    EnableWindow(hCheckbox, !info.isPolicyManaged);
}

static void ApplyCurrentSettings(HWND hWnd) {
    bool requestedAutostart = (IsDlgButtonChecked(hWnd, IDC_CHK_AUTOSTART) == BST_CHECKED);
    bool initialAutostartEnabled = s_autostartStatusAtOpen == AUTOSTART_ENABLED;

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
    g_config.clickThrough    = (IsDlgButtonChecked(hWnd, IDC_CHK_CLICKTHROUGH) == BST_CHECKED);
    g_config.useLocalSensors = (IsDlgButtonChecked(hWnd, IDC_CHK_LOCAL_SENSORS) == BST_CHECKED);

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
    if (g_config.refreshRateMs > 10000) g_config.refreshRateMs = 10000;
    if (g_config.itemSpacing < 0) g_config.itemSpacing = 0;
    if (g_config.itemSpacing > 100) g_config.itemSpacing = 100;
    if (g_config.fontSize < 1)  g_config.fontSize = 1;
    if (g_config.fontSize > 24) g_config.fontSize = 24;
    SetDlgItemInt(hWnd, IDC_EDIT_SPACING, (UINT)g_config.itemSpacing, FALSE);
    SetDlgItemInt(hWnd, IDC_EDIT_FONTSIZE, (UINT)g_config.fontSize, FALSE);
    SetDlgItemInt(hWnd, IDC_EDIT_RATE, (UINT)g_config.refreshRateMs, FALSE);

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

    if (requestedAutostart != initialAutostartEnabled) {
        if (!SetAutostart(requestedAutostart)) {
            MessageBoxW(hWnd,
                L"Windows did not accept the startup change. Review Windows Startup Apps and try again if needed.",
                L"TaskbarMonitor Startup", MB_OK | MB_ICONWARNING);
        }
    }
    g_config.runAtStartup = IsAutostartEnabled();
    s_autostartCheckboxDirty = false;

    SaveConfig();
    SaveSettingsThemeMode();
    RefreshAutostartControls(hWnd, true);

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
        s_colLabel   = g_config.colLabel;
        s_colValue   = g_config.colValue;
        s_colNetUp   = g_config.colNetUp;
        s_colNetDown = g_config.colNetDown;
        s_colDivider = g_config.colDivider;
        s_colBg      = g_config.colBackground;

        UpdateSettingsTheme(hWnd);

        CreateWindowExW(0, L"STATIC", L"Metrics", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
            0, 0, 0, 0, hWnd, (HMENU)IDC_PAGE_TITLE, NULL, NULL);
        CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
            0, 0, 0, 0, hWnd, (HMENU)IDC_PAGE_DESCRIPTION, NULL, NULL);

        const wchar_t* tabTitles[] = { L"Metrics", L"Position", L"Text", L"Colors", L"System" };
        for (int i = 0; i < 5; i++) {
            CreateWindowExW(0, L"BUTTON", tabTitles[i],
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 0, 0, hWnd, (HMENU)(UINT_PTR)(IDC_TAB_BTN_BASE + i), NULL, NULL);
        }

        // --- TAB 0: Metrics (Structured Two-Column Grid) ---
        CreateWindowExW(0, L"BUTTON", L"Network speed", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 36, 62, 205, 22, hWnd, (HMENU)IDC_CHK_NET, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"CPU use", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 36, 92, 205, 22, hWnd, (HMENU)IDC_CHK_CPU, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"GPU use", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 36, 122, 205, 22, hWnd, (HMENU)IDC_CHK_GPU, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Memory", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 36, 152, 205, 22, hWnd, (HMENU)IDC_CHK_RAM, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Battery", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 36, 182, 205, 22, hWnd, (HMENU)IDC_CHK_BATTERY, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"CPU temperature", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 255, 62, 205, 22, hWnd, (HMENU)IDC_CHK_CPUTEMP, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"GPU temperature", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 255, 92, 205, 22, hWnd, (HMENU)IDC_CHK_GPUTEMP, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Disk activity and free space", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 255, 122, 205, 22, hWnd, (HMENU)IDC_CHK_DISK, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Uptime", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 255, 152, 205, 22, hWnd, (HMENU)IDC_CHK_UPTIME, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Drive", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 230, 180, 20, hWnd, (HMENU)IDC_LBL_DRIVE, NULL, NULL);
        HWND hComboDrive = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 235, 226, 120, 140, hWnd, (HMENU)IDC_COMBO_DRIVE, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Network units", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 268, 180, 20, hWnd, (HMENU)IDC_LBL_NETUNIT, NULL, NULL);
        HWND hComboNetU  = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST, 235, 264, 195, 100, hWnd, (HMENU)IDC_COMBO_NETUNIT, NULL, NULL);
        CreateWindowExW(0, L"STATIC", L"Estimated temperatures show ~ when sensors are unavailable.", WS_CHILD | SS_LEFT | SS_NOPREFIX,
            0, 0, 0, 0, hWnd, (HMENU)IDC_LBL_METRIC_NOTE, NULL, NULL);

        // --- TAB 1: Position and Layout ---
        CreateWindowExW(0, L"STATIC", L"Taskbar position", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 70, 180, 20, hWnd, (HMENU)IDC_LBL_ALIGN, NULL, NULL);
        HWND hComboAlign = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST, 235, 66, 195, 120, hWnd, (HMENU)IDC_COMBO_ALIGN, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Horizontal offset (px)", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 110, 180, 20, hWnd, (HMENU)IDC_LBL_OFFSETX, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"12", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 235, 106, 95, 24, hWnd, (HMENU)IDC_EDIT_OFFSETX, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Vertical offset (px)", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 150, 180, 20, hWnd, (HMENU)IDC_LBL_OFFSETY, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 235, 146, 95, 24, hWnd, (HMENU)IDC_EDIT_OFFSETY, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Reading gap (px)", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 190, 180, 20, hWnd, (HMENU)IDC_LBL_SPACING, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10", WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 235, 186, 95, 24, hWnd, (HMENU)IDC_EDIT_SPACING, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Show dividers between readings", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 36, 238, 420, 22, hWnd, (HMENU)IDC_CHK_DIVIDERS, NULL, NULL);
        CreateWindowExW(0, L"STATIC", L"Offsets adjust the selected position; Custom uses screen coordinates.",
            WS_CHILD | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, hWnd, (HMENU)IDC_LBL_LAYOUT_NOTE, NULL, NULL);

        // --- TAB 2: Typography ---
        CreateWindowExW(0, L"STATIC", L"Font", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 75, 180, 20, hWnd, (HMENU)IDC_LBL_FONT, NULL, NULL);
        HWND hComboFont = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_TABSTOP | CBS_DROPDOWN | WS_VSCROLL, 235, 71, 215, 220, hWnd, (HMENU)IDC_COMBO_FONT, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Size (pt)", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 120, 180, 20, hWnd, (HMENU)IDC_LBL_FONTSIZE, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"11", WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 235, 116, 95, 24, hWnd, (HMENU)IDC_EDIT_FONTSIZE, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Weight", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 165, 180, 20, hWnd, (HMENU)IDC_LBL_FONTWEIGHT, NULL, NULL);
        HWND hComboWeight = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST, 235, 161, 180, 120, hWnd, (HMENU)IDC_COMBO_FONTWEIGHT, NULL, NULL);
        CreateWindowExW(0, L"STATIC", L"Font preview · current CPU and memory readings", WS_CHILD | SS_LEFT | SS_NOPREFIX,
            0, 0, 0, 0, hWnd, (HMENU)IDC_LBL_TYPE_NOTE, NULL, NULL);

        // --- TAB 3: Theme and Colors ---
        CreateWindowExW(0, L"STATIC", L"Monitor palette", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 66, 180, 20, hWnd, (HMENU)IDC_LBL_THEME, NULL, NULL);
        HWND hComboTheme = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST, 235, 62, 200, 120, hWnd, (HMENU)IDC_COMBO_THEME, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Transparent taskbar background", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 36, 100, 420, 22, hWnd, (HMENU)IDC_CHK_TRANS_BG, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Adjust text contrast automatically", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 36, 128, 420, 22, hWnd, (HMENU)IDC_CHK_AUTOCONTRAST, NULL, NULL);
        CreateWindowExW(0, L"STATIC", L"Select Custom to edit individual colors.", WS_CHILD | SS_LEFT | SS_NOPREFIX,
            0, 0, 0, 0, hWnd, (HMENU)IDC_LBL_COLOR_NOTE, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Labels", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 36, 166, 200, 32, hWnd, (HMENU)IDC_BTN_COL_LABEL, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Values", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 250, 166, 200, 32, hWnd, (HMENU)IDC_BTN_COL_VALUE, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Upload (▲)", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 36, 206, 200, 32, hWnd, (HMENU)IDC_BTN_COL_UP, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Download (▼)", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 250, 206, 200, 32, hWnd, (HMENU)IDC_BTN_COL_DOWN, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Dividers", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 36, 246, 200, 32, hWnd, (HMENU)IDC_BTN_COL_DIV, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Background", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 250, 246, 200, 32, hWnd, (HMENU)IDC_BTN_COL_BG, NULL, NULL);

        // --- TAB 4: Advanced ---
        CreateWindowExW(0, L"STATIC", L"Refresh interval (ms)", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 75, 180, 20, hWnd, (HMENU)IDC_LBL_RATE, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1000", WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 235, 71, 105, 24, hWnd, (HMENU)IDC_EDIT_RATE, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Settings theme", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 120, 180, 20, hWnd, (HMENU)IDC_LBL_SETTINGSTHEME, NULL, NULL);
        HWND hComboSettingsTheme = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST, 235, 116, 195, 120, hWnd, (HMENU)IDC_COMBO_SETTINGSTHEME, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Start with Windows", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 36, 168, 420, 22, hWnd, (HMENU)IDC_CHK_AUTOSTART, NULL, NULL);
        CreateWindowExW(0, L"STATIC", L"Startup status: Checking...", WS_CHILD | SS_LEFT | SS_NOPREFIX, 36, 194, 420, 20, hWnd, (HMENU)IDC_LBL_AUTOSTART_STATUS, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Let clicks pass through to the taskbar", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 36, 222, 420, 22, hWnd, (HMENU)IDC_CHK_CLICKTHROUGH, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Use LibreHardwareMonitor temperatures", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 36, 258, 445, 22, hWnd, (HMENU)IDC_CHK_LOCAL_SENSORS, NULL, NULL);
        CreateWindowExW(0, L"STATIC", L"Requires its local web server on port 8085.", WS_CHILD | SS_LEFT | SS_NOPREFIX,
            0, 0, 0, 0, hWnd, (HMENU)IDC_LBL_SENSOR_NOTE, NULL, NULL);

        // Bottom Action Buttons (Flush and Proportional)
        CreateWindowExW(0, L"BUTTON", L"Reset to defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 16, 412, 95, 34, hWnd, (HMENU)IDC_BTN_DEFAULTS, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 198, 412, 88, 34, hWnd, (HMENU)IDC_BTN_APPLY, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 294, 412, 88, 34, hWnd, (HMENU)IDC_BTN_CANCEL, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Save and close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 390, 412, 116, 34, hWnd, (HMENU)IDC_BTN_SAVE, NULL, NULL);

        SetSettingsFonts(hWnd);
        LayoutSettingsControls(hWnd);

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
        CheckDlgButton(hWnd, IDC_CHK_LOCAL_SENSORS, g_config.useLocalSensors ? BST_CHECKED : BST_UNCHECKED);
        s_autostartCheckboxDirty = false;

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

        SendMessageW(hComboNetU, CB_ADDSTRING, 0, (LPARAM)L"Bytes per second");
        SendMessageW(hComboNetU, CB_ADDSTRING, 0, (LPARAM)L"Bits per second");
        SendMessageW(hComboNetU, CB_SETCURSEL, g_config.netUnit, 0);

        SendMessageW(hComboAlign, CB_ADDSTRING, 0, (LPARAM)L"Left");
        SendMessageW(hComboAlign, CB_ADDSTRING, 0, (LPARAM)L"Next to system tray");
        SendMessageW(hComboAlign, CB_ADDSTRING, 0, (LPARAM)L"Center");
        SendMessageW(hComboAlign, CB_ADDSTRING, 0, (LPARAM)L"Custom coordinates");
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

        SendMessageW(hComboWeight, CB_ADDSTRING, 0, (LPARAM)L"Normal");
        SendMessageW(hComboWeight, CB_ADDSTRING, 0, (LPARAM)L"Medium");
        SendMessageW(hComboWeight, CB_ADDSTRING, 0, (LPARAM)L"Semi-bold");
        SendMessageW(hComboWeight, CB_ADDSTRING, 0, (LPARAM)L"Bold");
        int wSel = (g_config.fontWeight == FW_BOLD) ? 3 : (g_config.fontWeight == FW_SEMIBOLD ? 2 : (g_config.fontWeight == FW_MEDIUM ? 1 : 0));
        SendMessageW(hComboWeight, CB_SETCURSEL, wSel, 0);

        SendMessageW(hComboTheme, CB_ADDSTRING, 0, (LPARAM)L"Follow Windows");
        SendMessageW(hComboTheme, CB_ADDSTRING, 0, (LPARAM)L"Dark");
        SendMessageW(hComboTheme, CB_ADDSTRING, 0, (LPARAM)L"Light");
        SendMessageW(hComboTheme, CB_ADDSTRING, 0, (LPARAM)L"Custom");
        SendMessageW(hComboTheme, CB_SETCURSEL, g_config.themeMode, 0);

        SendMessageW(hComboSettingsTheme, CB_ADDSTRING, 0, (LPARAM)L"Follow Windows");
        SendMessageW(hComboSettingsTheme, CB_ADDSTRING, 0, (LPARAM)L"Dark");
        SendMessageW(hComboSettingsTheme, CB_ADDSTRING, 0, (LPARAM)L"Light");
        SendMessageW(hComboSettingsTheme, CB_SETCURSEL, g_settingsThemeMode, 0);

        RefreshAutostartControls(hWnd, true);
        UpdateColorPickerAvailability(hWnd);
        ShowTabControls(hWnd, 0);
        break;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            RefreshAutostartControls(hWnd, !s_autostartCheckboxDirty);
        }
        break;

    case WM_DPICHANGED: {
        s_dpi = HIWORD(wParam);
        RECT* suggested = (RECT*)lParam;
        SetWindowPos(hWnd, NULL, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetSettingsFonts(hWnd);
        LayoutSettingsControls(hWnd);
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
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
        RECT client;
        GetClientRect(hWnd, &client);
        RECT sidebar = { 0, 0, Px(180), client.bottom };
        FillRect(hdc, &sidebar, hCardBrush);

        HPEN oldPen = (HPEN)SelectObject(hdc, hCardBorderPen);
        MoveToEx(hdc, Px(180), 0, NULL);
        LineTo(hdc, Px(180), client.bottom);
        MoveToEx(hdc, Px(14), Px(91), NULL);
        LineTo(hdc, Px(166), Px(91));
        MoveToEx(hdc, Px(202), Px(105), NULL);
        LineTo(hdc, client.right - Px(20), Px(105));
        MoveToEx(hdc, Px(202), Px(490), NULL);
        LineTo(hdc, client.right - Px(20), Px(490));
        SelectObject(hdc, oldPen);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, s_colTextPrimary);
        HFONT oldFont = (HFONT)SelectObject(hdc, hFontBtn);
        RECT brand = { Px(22), Px(28), Px(168), Px(52) };
        DrawTextW(hdc, L"TaskbarMonitor", -1, &brand, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(hdc, s_colTextSecondary);
        SelectObject(hdc, hFontBody);
        RECT caption = { Px(22), Px(55), Px(168), Px(80) };
        DrawTextW(hdc, L"Settings", -1, &caption, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
        DrawFontPreview(hWnd, hdc);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        int id = GetDlgCtrlID((HWND)lParam);
        bool secondary = id == IDC_PAGE_DESCRIPTION || id == IDC_LBL_METRIC_NOTE ||
                         id == IDC_LBL_LAYOUT_NOTE || id == IDC_LBL_TYPE_NOTE ||
                         id == IDC_LBL_COLOR_NOTE || id == IDC_LBL_SENSOR_NOTE ||
                         id == IDC_LBL_AUTOSTART_STATUS;
        SetTextColor(hdcStatic, secondary ? s_colTextSecondary : s_colTextPrimary);
        SetBkColor(hdcStatic, s_colWindowBg);
        return (LRESULT)hBgBrush;
    }

    case WM_CTLCOLORBTN: {
        HDC hdcBtn = (HDC)wParam;
        SetBkMode(hdcBtn, TRANSPARENT);
        SetTextColor(hdcBtn, s_colTextPrimary);
        return (LRESULT)hBgBrush;
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

        // Navigation follows the settings pages, without a second row of tabs.
        if (dis->CtlID >= IDC_TAB_0 && dis->CtlID <= IDC_TAB_4) {
            int tabIndex = dis->CtlID - IDC_TAB_BTN_BASE;
            bool isSelected = (g_currentTab == tabIndex);
            FillRect(dis->hDC, &dis->rcItem, hCardBrush);
            if (isSelected) {
                FillRect(dis->hDC, &dis->rcItem, hControlBrush);
                RECT bar = { dis->rcItem.left, dis->rcItem.top + Px(7),
                             dis->rcItem.left + Px(3), dis->rcItem.bottom - Px(7) };
                HBRUSH accent = CreateSolidBrush(s_colAccent);
                FillRect(dis->hDC, &bar, accent);
                DeleteObject(accent);
            }

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, isSelected ? s_colTextPrimary : s_colTextSecondary);
            SelectObject(dis->hDC, isSelected ? hFontBtn : hFontTab);

            wchar_t tabText[64];
            GetWindowTextW(dis->hwndItem, tabText, 64);
            RECT label = dis->rcItem;
            label.left += Px(18);
            DrawTextW(dis->hDC, tabText, -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (dis->itemState & ODS_FOCUS) {
                RECT focus = dis->rcItem;
                InflateRect(&focus, -Px(4), -Px(4));
                DrawFocusRect(dis->hDC, &focus);
            }
            return TRUE;
        }

        // Persistent actions stay in the footer on every page.
        if (dis->CtlID == IDC_BTN_SAVE) {
            bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
            COLORREF btnFill = isPressed
                ? RGB((GetRValue(s_colAccent) * 4) / 5, (GetGValue(s_colAccent) * 4) / 5, (GetBValue(s_colAccent) * 4) / 5)
                : s_colAccent;

            HBRUSH btnBrush = CreateSolidBrush(btnFill);
            HPEN btnPen = CreatePen(PS_SOLID, 1, btnFill);
            HGDIOBJ oBrush = SelectObject(dis->hDC, btnBrush);
            HGDIOBJ oPen = SelectObject(dis->hDC, btnPen);
            RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, Px(6), Px(6));
            SelectObject(dis->hDC, oBrush);
            SelectObject(dis->hDC, oPen);
            DeleteObject(btnBrush);
            DeleteObject(btnPen);

            double lum = (0.299 * GetRValue(s_colAccent) + 0.587 * GetGValue(s_colAccent) + 0.114 * GetBValue(s_colAccent)) / 255.0;
            COLORREF txtCol = (lum > 0.6) ? RGB(0, 0, 0) : RGB(255, 255, 255);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, txtCol);
            SelectObject(dis->hDC, hFontBtn);
            DrawTextW(dis->hDC, L"Save and close", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (dis->itemState & ODS_FOCUS) {
                RECT focus = dis->rcItem;
                InflateRect(&focus, -3, -3);
                DrawFocusRect(dis->hDC, &focus);
            }
            return TRUE;
        } else if (dis->CtlID == IDC_BTN_APPLY || dis->CtlID == IDC_BTN_CANCEL || dis->CtlID == IDC_BTN_DEFAULTS) {
            bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
            COLORREF btnFill = isPressed
                ? s_colBorder : s_colControlBg;

            HBRUSH btnBrush = CreateSolidBrush(btnFill);
            HPEN borderPen = CreatePen(PS_SOLID, 1, s_colBorder);
            HGDIOBJ oBrush = SelectObject(dis->hDC, btnBrush);
            HGDIOBJ oPen = SelectObject(dis->hDC, borderPen);
            RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, Px(6), Px(6));
            SelectObject(dis->hDC, oBrush);
            SelectObject(dis->hDC, oPen);
            DeleteObject(btnBrush);
            DeleteObject(borderPen);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, s_colTextPrimary);
            SelectObject(dis->hDC, hFontBtn);
            const wchar_t* btnLbl = (dis->CtlID == IDC_BTN_APPLY) ? L"Apply" : ((dis->CtlID == IDC_BTN_CANCEL) ? L"Cancel" : L"Reset to defaults");
            DrawTextW(dis->hDC, btnLbl, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (dis->itemState & ODS_FOCUS) {
                RECT focus = dis->rcItem;
                InflateRect(&focus, -3, -3);
                DrawFocusRect(dis->hDC, &focus);
            }
            return TRUE;
        }

        // Swatches remain useful when custom colors are selected.
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
            COLORREF btnFill = isPressed ? s_colControlBg : s_colWindowBg;

            HBRUSH cardBrush = CreateSolidBrush(btnFill);
            HPEN cardBorder = CreatePen(PS_SOLID, 1, s_colBorder);
            HGDIOBJ oB = SelectObject(dis->hDC, cardBrush);
            HGDIOBJ oP = SelectObject(dis->hDC, cardBorder);
            Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
            SelectObject(dis->hDC, oB);
            SelectObject(dis->hDC, oP);
            DeleteObject(cardBrush);
            DeleteObject(cardBorder);

            int swatchSize = Px(18);
            int swatchX = dis->rcItem.left + Px(11);
            int swatchY = dis->rcItem.top + (dis->rcItem.bottom - dis->rcItem.top - swatchSize) / 2;
            HBRUSH swatchBrush = CreateSolidBrush(c);
            HPEN swatchPen = CreatePen(PS_SOLID, 1, s_colBorder);
            HGDIOBJ oSB = SelectObject(dis->hDC, swatchBrush);
            HGDIOBJ oSP = SelectObject(dis->hDC, swatchPen);
            Rectangle(dis->hDC, swatchX, swatchY, swatchX + swatchSize, swatchY + swatchSize);
            SelectObject(dis->hDC, oSB);
            SelectObject(dis->hDC, oSP);
            DeleteObject(swatchBrush);
            DeleteObject(swatchPen);

            RECT textRc = dis->rcItem;
            textRc.left = swatchX + swatchSize + Px(11);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, (dis->itemState & ODS_DISABLED) ? s_colTextSecondary : s_colTextPrimary);
            SelectObject(dis->hDC, hFontBtn);
            DrawTextW(dis->hDC, lbl, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (dis->itemState & ODS_FOCUS) {
                RECT focus = dis->rcItem;
                InflateRect(&focus, -3, -3);
                DrawFocusRect(dis->hDC, &focus);
            }
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        if (id == IDC_CHK_AUTOSTART && HIWORD(wParam) == BN_CLICKED) {
            s_autostartCheckboxDirty = true;
            RefreshAutostartControls(hWnd, false);
            return 0;
        }

        if (id >= IDC_TAB_0 && id <= IDC_TAB_4) {
            ShowTabControls(hWnd, id - IDC_TAB_BTN_BASE);
            return 0;
        }

        if (id == IDC_COMBO_SETTINGSTHEME && HIWORD(wParam) == CBN_SELCHANGE) {
            g_settingsThemeMode = (int)SendMessageW(GetDlgItem(hWnd, IDC_COMBO_SETTINGSTHEME), CB_GETCURSEL, 0, 0);
            UpdateSettingsTheme(hWnd);
            return 0;
        }
        if (id == IDC_COMBO_SETTINGSTHEME && HIWORD(wParam) == CBN_CLOSEUP) {
            RefreshComboThemes(hWnd);
            return 0;
        }

        if (id == IDC_COMBO_THEME && HIWORD(wParam) == CBN_SELCHANGE) {
            UpdateColorPickerAvailability(hWnd);
            return 0;
        }

        if ((id == IDC_COMBO_FONT &&
             (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_EDITCHANGE)) ||
            (id == IDC_COMBO_FONTWEIGHT && HIWORD(wParam) == CBN_SELCHANGE) ||
            (id == IDC_EDIT_FONTSIZE && HIWORD(wParam) == EN_CHANGE)) {
            RECT preview = { Px(216), Px(368), Px(688), Px(458) };
            InvalidateRect(hWnd, &preview, TRUE);
            return 0;
        }

        if (id == IDC_BTN_COL_LABEL) PickColor(hWnd, s_colLabel, IDC_BTN_COL_LABEL);
        else if (id == IDC_BTN_COL_VALUE) PickColor(hWnd, s_colValue, IDC_BTN_COL_VALUE);
        else if (id == IDC_BTN_COL_UP) PickColor(hWnd, s_colNetUp, IDC_BTN_COL_UP);
        else if (id == IDC_BTN_COL_DOWN) PickColor(hWnd, s_colNetDown, IDC_BTN_COL_DOWN);
        else if (id == IDC_BTN_COL_DIV) PickColor(hWnd, s_colDivider, IDC_BTN_COL_DIV);
        else if (id == IDC_BTN_COL_BG) PickColor(hWnd, s_colBg, IDC_BTN_COL_BG);
        else if (id == IDC_BTN_DEFAULTS) {
            MonitorConfig saved = g_config;
            SetDefaults();
            DestroyWindow(hWnd);
            OpenSettingsWindow((HINSTANCE)GetWindowLongPtr(g_hOwnerWnd, GWLP_HINSTANCE), g_hOwnerWnd);
            g_config = saved;
            if (g_hSettingsWnd) {
                g_settingsThemeMode = 0;
                SendMessageW(GetDlgItem(g_hSettingsWnd, IDC_COMBO_SETTINGSTHEME), CB_SETCURSEL, 0, 0);
                UpdateSettingsTheme(g_hSettingsWnd);
            }
        } else if (id == IDC_BTN_APPLY) {
            ApplyCurrentSettings(hWnd);
        } else if (id == IDC_BTN_SAVE) {
            ApplyCurrentSettings(hWnd);
            DestroyWindow(hWnd);
        } else if (id == IDC_BTN_CANCEL || id == IDCANCEL) {
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
        s_autostartCheckboxDirty = false;
        s_autostartStatusAtOpen = AUTOSTART_UNAVAILABLE;
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
        RefreshAutostartControls(g_hSettingsWnd, !s_autostartCheckboxDirty);
        SetForegroundWindow(g_hSettingsWnd);
        return;
    }

    g_settingsThemeMode = ReadSettingsThemeMode();

    WNDCLASSEXW swc = { sizeof(WNDCLASSEXW) };
    swc.lpfnWndProc = SettingsWndProc;
    swc.hInstance = hInstance;
    swc.lpszClassName = L"TaskbarMonitorSettings";
    swc.hCursor = LoadCursor(NULL, IDC_ARROW);
    swc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_TASKBARMONITOR));
    swc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_TASKBARMONITOR),
                                   IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    RegisterClassExW(&swc);

    HDC screen = GetDC(NULL);
    s_dpi = screen ? (UINT)GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen) ReleaseDC(NULL, screen);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
    RECT frame = { 0, 0, Px(760), Px(560) };
    AdjustWindowRectEx(&frame, style, FALSE, 0);
    int winW = frame.right - frame.left;
    int winH = frame.bottom - frame.top;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    g_hSettingsWnd = CreateWindowExW(
        0,
        swc.lpszClassName,
        L"TaskbarMonitor Settings",
        style,
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
