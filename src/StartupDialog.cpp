#include "StartupDialog.h"
#include "Resource.h"

#define IDC_STARTUP_BODY       1601
#define IDC_STARTUP_STATUS     1602
#define IDC_STARTUP_ENABLE     1603
#define IDC_STARTUP_KEEP       1604

static HWND g_hStartupWnd = NULL;
static bool g_startupEnabled = false;
static AutostartInfo g_startupInfo = { AUTOSTART_UNAVAILABLE, false, false, false };

static bool s_isDarkMode = true;
static COLORREF s_colWindowBg = RGB(32, 32, 32);
static COLORREF s_colCardBg = RGB(44, 44, 44);
static COLORREF s_colBorder = RGB(58, 58, 62);
static COLORREF s_colTextPrimary = RGB(255, 255, 255);
static COLORREF s_colTextSecondary = RGB(160, 160, 160);
static COLORREF s_colAccent = RGB(96, 205, 255);

static HFONT hTitleFont = NULL;
static HFONT hBodyFont = NULL;
static HFONT hButtonFont = NULL;
static HBRUSH hWindowBrush = NULL;
static HBRUSH hCardBrush = NULL;

static void UpdateDialogTheme(HWND hWnd) {
    DWORD isLight = 0;
    DWORD size = sizeof(DWORD);
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"SystemUsesLightTheme", NULL, NULL, (LPBYTE)&isLight, &size);
        RegCloseKey(hKey);
    }

    s_isDarkMode = (isLight == 0);
    if (s_isDarkMode) {
        s_colWindowBg = RGB(32, 32, 32);
        s_colCardBg = RGB(44, 44, 44);
        s_colBorder = RGB(58, 58, 62);
        s_colTextPrimary = RGB(255, 255, 255);
        s_colTextSecondary = RGB(160, 160, 160);
        s_colAccent = RGB(96, 205, 255);
    } else {
        s_colWindowBg = RGB(243, 243, 243);
        s_colCardBg = RGB(255, 255, 255);
        s_colBorder = RGB(210, 210, 215);
        s_colTextPrimary = RGB(26, 26, 26);
        s_colTextSecondary = RGB(80, 80, 80);
        s_colAccent = RGB(0, 95, 184);
    }

    BOOL dwmDark = s_isDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dwmDark, sizeof(dwmDark));

    if (hWindowBrush) DeleteObject(hWindowBrush);
    if (hCardBrush) DeleteObject(hCardBrush);
    hWindowBrush = CreateSolidBrush(s_colWindowBg);
    hCardBrush = CreateSolidBrush(s_colCardBg);
    InvalidateRect(hWnd, NULL, TRUE);
}

static const wchar_t* GetStatusText(const AutostartInfo& info) {
    switch (info.status) {
    case AUTOSTART_NOT_CONFIGURED:
        return L"Windows startup: Not configured";
    case AUTOSTART_ENABLED:
        return L"Windows startup: Enabled";
    case AUTOSTART_DISABLED_BY_SYSTEM:
        return L"Windows startup: Disabled by Windows";
    case AUTOSTART_OTHER_PATH:
        return L"Windows startup: Another copy is configured";
    case AUTOSTART_UNAVAILABLE:
    default:
        if (info.isPolicyManaged) return L"Windows startup: Controlled by system policy";
        return L"Windows startup: Unable to verify";
    }
}

static const wchar_t* GetBodyText(const AutostartInfo& info) {
    switch (info.status) {
    case AUTOSTART_NOT_CONFIGURED:
        return L"TaskbarMonitor is not registered to start when you sign in to Windows.\r\n\r\nChoose Enable Startup to add it, or Keep Disabled to leave startup unchanged.";
    case AUTOSTART_DISABLED_BY_SYSTEM:
        return L"TaskbarMonitor is registered for startup, but Windows Startup Apps has disabled it.\r\n\r\nChoose Enable Startup to repair that setting, or Keep Disabled to respect it.";
    case AUTOSTART_OTHER_PATH:
        return L"Windows startup points to another copy of TaskbarMonitor.\r\n\r\nChoose Enable Startup to use this copy, or Keep Disabled to leave the existing entry unchanged.";
    case AUTOSTART_UNAVAILABLE:
        if (info.isPolicyManaged) {
            return L"Windows controls this startup entry and it cannot be changed here.\r\n\r\nYou can review the setting in Windows Startup Apps.";
        }
        return L"TaskbarMonitor could not verify its Windows startup state.\r\n\r\nChoose Enable Startup to try to repair it, or Keep Disabled to leave the current setting unchanged.";
    case AUTOSTART_ENABLED:
    default:
        return L"TaskbarMonitor startup is enabled.";
    }
}

static void RefreshDialogText(HWND hWnd) {
    SetDlgItemTextW(hWnd, IDC_STARTUP_BODY, GetBodyText(g_startupInfo));
    SetDlgItemTextW(hWnd, IDC_STARTUP_STATUS, GetStatusText(g_startupInfo));
}

static void CloseWithoutChange(HWND hWnd) {
    g_startupEnabled = false;
    DestroyWindow(hWnd);
}

static LRESULT CALLBACK StartupWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        DWORD cornerPref = 2;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

        hTitleFont = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        hBodyFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
        hButtonFont = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

        UpdateDialogTheme(hWnd);

        CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
            32, 72, 456, 76, hWnd, (HMENU)IDC_STARTUP_BODY, NULL, NULL);
        CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
            32, 154, 456, 24, hWnd, (HMENU)IDC_STARTUP_STATUS, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Enable Startup",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
            246, 204, 114, 36, hWnd, (HMENU)IDC_STARTUP_ENABLE, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Keep Disabled",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            374, 204, 114, 36, hWnd, (HMENU)IDC_STARTUP_KEEP, NULL, NULL);

        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, lParam, TRUE);
            return TRUE;
        }, (LPARAM)hBodyFont);
        SendMessageW(GetDlgItem(hWnd, IDC_STARTUP_ENABLE), WM_SETFONT, (WPARAM)hButtonFont, TRUE);
        SendMessageW(GetDlgItem(hWnd, IDC_STARTUP_KEEP), WM_SETFONT, (WPARAM)hButtonFont, TRUE);

        HWND hEnableButton = GetDlgItem(hWnd, IDC_STARTUP_ENABLE);
        if (g_startupInfo.isPolicyManaged) {
            EnableWindow(hEnableButton, FALSE);
        }

        RefreshDialogText(hWnd);
        SetFocus(g_startupInfo.isPolicyManaged
            ? GetDlgItem(hWnd, IDC_STARTUP_KEEP)
            : hEnableButton);
        break;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, hWindowBrush);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);
        HPEN borderPen = CreatePen(PS_SOLID, 1, s_colBorder);
        HGDIOBJ oldBrush = SelectObject(hdc, hCardBrush);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        RoundRect(hdc, 16, 12, rc.right - 16, 58, 8, 8);
        RoundRect(hdc, 16, 64, rc.right - 16, 190, 10, 10);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        RECT titleRc = { 32, 12, rc.right - 32, 58 };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, s_colTextPrimary);
        SelectObject(hdc, hTitleFont);
        DrawTextW(hdc, L"TaskbarMonitor Startup", -1, &titleRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hStatic = (HWND)lParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        SetBkColor(hdcStatic, s_colCardBg);
        SetTextColor(hdcStatic,
            GetDlgCtrlID(hStatic) == IDC_STARTUP_STATUS ? s_colTextSecondary : s_colTextPrimary);
        return (LRESULT)hCardBrush;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID != IDC_STARTUP_ENABLE && dis->CtlID != IDC_STARTUP_KEEP) break;

        bool isPrimary = dis->CtlID == IDC_STARTUP_ENABLE;
        bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
        bool isDisabled = (dis->itemState & ODS_DISABLED) != 0;
        COLORREF fill = isDisabled
            ? (s_isDarkMode ? RGB(55, 55, 60) : RGB(235, 235, 238))
            : (isPrimary ? s_colAccent
            : (s_isDarkMode ? RGB(44, 44, 48) : RGB(250, 250, 252)));
        if (isDisabled) isPressed = false;
        if (isPressed) {
            fill = isPrimary
                ? RGB((GetRValue(s_colAccent) * 4) / 5,
                      (GetGValue(s_colAccent) * 4) / 5,
                      (GetBValue(s_colAccent) * 4) / 5)
                : (s_isDarkMode ? RGB(56, 56, 62) : RGB(225, 227, 232));
        }

        HBRUSH buttonBrush = CreateSolidBrush(fill);
        HPEN buttonPen = CreatePen(PS_SOLID, 1, isPrimary ? fill : s_colBorder);
        HGDIOBJ oldBrush = SelectObject(dis->hDC, buttonBrush);
        HGDIOBJ oldPen = SelectObject(dis->hDC, buttonPen);
        RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
            dis->rcItem.right, dis->rcItem.bottom, 8, 8);
        SelectObject(dis->hDC, oldBrush);
        SelectObject(dis->hDC, oldPen);
        DeleteObject(buttonBrush);
        DeleteObject(buttonPen);

        double luminance = (0.299 * GetRValue(fill) + 0.587 * GetGValue(fill) + 0.114 * GetBValue(fill)) / 255.0;
        COLORREF textColor = isDisabled
            ? s_colTextSecondary
            : ((luminance > 0.6) ? RGB(0, 0, 0) : RGB(255, 255, 255));
        if (!isPrimary && !isDisabled) textColor = s_colTextPrimary;
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, textColor);
        SelectObject(dis->hDC, hButtonFont);
        wchar_t buttonText[64] = { 0 };
        GetWindowTextW(dis->hwndItem, buttonText, _countof(buttonText));
        DrawTextW(dis->hDC, buttonText, -1, &dis->rcItem,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (dis->itemState & ODS_FOCUS) {
            RECT focusRect = dis->rcItem;
            InflateRect(&focusRect, -4, -4);
            DrawFocusRect(dis->hDC, &focusRect);
        }
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_STARTUP_ENABLE && HIWORD(wParam) == BN_CLICKED) {
            if (SetAutostart(true)) {
                g_startupEnabled = true;
                DestroyWindow(hWnd);
            } else {
                g_startupInfo = QueryAutostartStatus();
                SetDlgItemTextW(hWnd, IDC_STARTUP_BODY,
                    L"Windows did not accept the startup change.\r\n\r\nReview Windows Startup Apps, then try again if needed.");
                SetDlgItemTextW(hWnd, IDC_STARTUP_STATUS, GetStatusText(g_startupInfo));
                SetFocus(GetDlgItem(hWnd, IDC_STARTUP_ENABLE));
                InvalidateRect(hWnd, NULL, TRUE);
            }
        } else if (id == IDC_STARTUP_KEEP && HIWORD(wParam) == BN_CLICKED) {
            CloseWithoutChange(hWnd);
        }
        break;
    }

    case WM_CLOSE:
        CloseWithoutChange(hWnd);
        break;

    case WM_DESTROY:
        if (hTitleFont) DeleteObject(hTitleFont);
        if (hBodyFont) DeleteObject(hBodyFont);
        if (hButtonFont) DeleteObject(hButtonFont);
        if (hWindowBrush) DeleteObject(hWindowBrush);
        if (hCardBrush) DeleteObject(hCardBrush);
        hTitleFont = NULL;
        hBodyFont = NULL;
        hButtonFont = NULL;
        hWindowBrush = NULL;
        hCardBrush = NULL;
        g_hStartupWnd = NULL;
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

bool ShowAutostartDialog(HINSTANCE hInstance, HWND, const AutostartInfo& initialInfo) {
    g_startupEnabled = false;
    g_startupInfo = initialInfo;

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = StartupWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TaskbarMonitorStartupDialog";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_TASKBARMONITOR));
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_TASKBARMONITOR),
                                  IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    RegisterClassExW(&wc);

    RECT workArea = { 0 };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int winW = 520;
    int winH = 280;
    int posX = workArea.left + ((workArea.right - workArea.left) - winW) / 2;
    int posY = workArea.top + ((workArea.bottom - workArea.top) - winH) / 2;

    g_hStartupWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"TaskbarMonitor Startup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        posX, posY, winW, winH,
        NULL, NULL, hInstance, NULL
    );
    if (!g_hStartupWnd) {
        UnregisterClassW(wc.lpszClassName, hInstance);
        return false;
    }

    SetForegroundWindow(g_hStartupWnd);
    ShowWindow(g_hStartupWnd, SW_SHOW);
    UpdateWindow(g_hStartupWnd);

    MSG msg;
    while (g_hStartupWnd && IsWindow(g_hStartupWnd)) {
        BOOL messageResult = GetMessageW(&msg, NULL, 0, 0);
        if (messageResult <= 0) {
            g_hStartupWnd = NULL;
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageW(g_hStartupWnd, WM_CLOSE, 0, 0);
            continue;
        }
        if (!IsDialogMessageW(g_hStartupWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    UnregisterClassW(wc.lpszClassName, hInstance);
    return g_startupEnabled;
}
