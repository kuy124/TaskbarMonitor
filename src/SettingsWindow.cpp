#include "SettingsWindow.h"
#include "Config.h"
#include "Theme.h"
#include "Metrics.h"
#include "TaskbarSync.h"

HWND g_hSettingsWnd = NULL;
static HWND g_hOwnerWnd = NULL;

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HFONT hFontTitle = NULL;
    static HFONT hFontBody = NULL;
    static HFONT hFontBtn = NULL;
    static HBRUSH hBgBrush = NULL;
    static HBRUSH hCardBrush = NULL;
    static HPEN hCardBorderPen = NULL;

    switch (msg) {
    case WM_CREATE: {
        BOOL darkMode = TRUE;
        DWORD isLight = 0, size = sizeof(DWORD);
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExW(hKey, L"SystemUsesLightTheme", NULL, NULL, (LPBYTE)&isLight, &size);
            RegCloseKey(hKey);
        }
        darkMode = (isLight == 0);
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
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

        if (darkMode) {
            hBgBrush = CreateSolidBrush(RGB(32, 32, 32));
            hCardBrush = CreateSolidBrush(RGB(43, 43, 43));
            hCardBorderPen = CreatePen(PS_SOLID, 1, RGB(58, 58, 62));
        } else {
            hBgBrush = CreateSolidBrush(RGB(243, 243, 243));
            hCardBrush = CreateSolidBrush(RGB(255, 255, 255));
            hCardBorderPen = CreatePen(PS_SOLID, 1, RGB(229, 229, 229));
        }

        CreateWindowExW(0, L"BUTTON", L"Network Traffic (Upload / Download)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            34, 46, 310, 20, hWnd, (HMENU)IDC_CHK_NET, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Processors (CPU % & GPU %)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            34, 72, 310, 20, hWnd, (HMENU)IDC_CHK_COMPUTE, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Memory (RAM % & Used GB)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            34, 98, 310, 20, hWnd, (HMENU)IDC_CHK_MEMORY, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Storage (Disk Activity & Free Space)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            34, 124, 310, 20, hWnd, (HMENU)IDC_CHK_DISK, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"System Stats (Processes & Uptime / Battery)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            34, 150, 310, 20, hWnd, (HMENU)IDC_CHK_SYSTEM, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Target Drive to Monitor",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            34, 218, 170, 18, hWnd, NULL, NULL, NULL);

        HWND hComboDrive = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            220, 214, 124, 120, hWnd, (HMENU)IDC_COMBO_DRIVE, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Left Margin Offset",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            34, 252, 170, 18, hWnd, NULL, NULL, NULL);

        HWND hComboMargin = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            220, 248, 124, 120, hWnd, (HMENU)IDC_COMBO_MARGIN, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Update Polling Rate",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            34, 286, 170, 18, hWnd, NULL, NULL, NULL);

        HWND hComboRate = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            220, 282, 124, 120, hWnd, (HMENU)IDC_COMBO_RATE, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Apply & Save",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            166, 342, 108, 32, hWnd, (HMENU)IDC_BTN_SAVE, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            282, 342, 82, 32, hWnd, (HMENU)IDC_BTN_CANCEL, NULL, NULL);

        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, lParam, TRUE);
            return TRUE;
        }, (LPARAM)hFontBody);

        CheckDlgButton(hWnd, IDC_CHK_NET, g_config.showNet ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_COMPUTE, g_config.showCompute ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_MEMORY, g_config.showMemory ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_DISK, g_config.showDisk ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHK_SYSTEM, g_config.showSystem ? BST_CHECKED : BST_UNCHECKED);

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

        int margins[] = { 4, 8, 12, 16, 24, 32, 48, 64, 96, 140 };
        int marginSel = 1;
        for (int i = 0; i < 10; i++) {
            wchar_t buf[32];
            swprintf(buf, 32, L"%d px", margins[i]);
            SendMessageW(hComboMargin, CB_ADDSTRING, 0, (LPARAM)buf);
            if (margins[i] == g_config.leftMargin) marginSel = i;
        }
        SendMessageW(hComboMargin, CB_SETCURSEL, marginSel, 0);

        SendMessageW(hComboRate, CB_ADDSTRING, 0, (LPARAM)L"500 ms (Fast)");
        SendMessageW(hComboRate, CB_ADDSTRING, 0, (LPARAM)L"1000 ms (Normal)");
        SendMessageW(hComboRate, CB_ADDSTRING, 0, (LPARAM)L"2000 ms (Eco)");
        int rateSel = (g_config.refreshRateMs == 500) ? 0 : (g_config.refreshRateMs == 2000 ? 2 : 1);
        SendMessageW(hComboRate, CB_SETCURSEL, rateSel, 0);
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
        SetBkMode(hdc, TRANSPARENT);

        HGDIOBJ oldBrush = SelectObject(hdc, hCardBrush);
        HGDIOBJ oldPen = SelectObject(hdc, hCardBorderPen);

        RoundRect(hdc, 16, 14, 364, 182, 8, 8);
        RoundRect(hdc, 16, 194, 364, 326, 8, 8);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);

        HGDIOBJ oldFont = SelectObject(hdc, hFontTitle);
        SetTextColor(hdc, RGB(255, 255, 255));
        TextOutW(hdc, 32, 22, L"Displayed Metrics", 17);
        TextOutW(hdc, 32, 202, L"Hardware & Customization", 24);

        SelectObject(hdc, oldFont);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        SetTextColor(hdcStatic, RGB(235, 235, 235));
        return (LRESULT)hCardBrush;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_BTN_SAVE) {
            HBRUSH btnBrush = CreateSolidBrush(dis->itemState & ODS_SELECTED ? RGB(0, 90, 170) : RGB(0, 103, 192));
            HPEN nullPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
            HGDIOBJ ob = SelectObject(dis->hDC, btnBrush);
            HGDIOBJ op = SelectObject(dis->hDC, nullPen);

            RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, 6, 6);

            SelectObject(dis->hDC, ob);
            SelectObject(dis->hDC, op);
            DeleteObject(btnBrush);
            DeleteObject(nullPen);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(255, 255, 255));
            HGDIOBJ of = SelectObject(dis->hDC, hFontBtn);
            DrawTextW(dis->hDC, L"Apply & Save", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, of);
            return TRUE;
        } else if (dis->CtlID == IDC_BTN_CANCEL) {
            HBRUSH btnBrush = CreateSolidBrush(dis->itemState & ODS_SELECTED ? RGB(55, 55, 60) : RGB(45, 45, 48));
            HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(65, 65, 70));
            HGDIOBJ ob = SelectObject(dis->hDC, btnBrush);
            HGDIOBJ op = SelectObject(dis->hDC, borderPen);

            RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, 6, 6);

            SelectObject(dis->hDC, ob);
            SelectObject(dis->hDC, op);
            DeleteObject(btnBrush);
            DeleteObject(borderPen);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(225, 225, 225));
            HGDIOBJ of = SelectObject(dis->hDC, hFontBtn);
            DrawTextW(dis->hDC, L"Cancel", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, of);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_SAVE) {
            g_config.showNet     = (IsDlgButtonChecked(hWnd, IDC_CHK_NET) == BST_CHECKED);
            g_config.showCompute = (IsDlgButtonChecked(hWnd, IDC_CHK_COMPUTE) == BST_CHECKED);
            g_config.showMemory  = (IsDlgButtonChecked(hWnd, IDC_CHK_MEMORY) == BST_CHECKED);
            g_config.showDisk    = (IsDlgButtonChecked(hWnd, IDC_CHK_DISK) == BST_CHECKED);
            g_config.showSystem  = (IsDlgButtonChecked(hWnd, IDC_CHK_SYSTEM) == BST_CHECKED);

            HWND hComboDrive = GetDlgItem(hWnd, IDC_COMBO_DRIVE);
            int driveSel = (int)SendMessageW(hComboDrive, CB_GETCURSEL, 0, 0);
            if (driveSel != CB_ERR) {
                SendMessageW(hComboDrive, CB_GETLBTEXT, driveSel, (LPARAM)g_config.targetDrive);
            }

            int margins[] = { 4, 8, 12, 16, 24, 32, 48, 64, 96, 140 };
            HWND hComboMargin = GetDlgItem(hWnd, IDC_COMBO_MARGIN);
            int marginSel = (int)SendMessageW(hComboMargin, CB_GETCURSEL, 0, 0);
            if (marginSel >= 0 && marginSel < 10) {
                g_config.leftMargin = margins[marginSel];
            }

            HWND hComboRate = GetDlgItem(hWnd, IDC_COMBO_RATE);
            int rateSel = (int)SendMessageW(hComboRate, CB_GETCURSEL, 0, 0);
            g_config.refreshRateMs = (rateSel == 0) ? 500 : (rateSel == 2 ? 2000 : 1000);

            SaveConfig();

            if (g_hOwnerWnd && IsWindow(g_hOwnerWnd)) {
                SetTimer(g_hOwnerWnd, TIMER_METRICS, g_config.refreshRateMs, NULL);
                g_curWidth = CalculateTotalWidth();
                UpdateDisk();
                UpdateThemeColors();
                SyncWithTaskbar(g_hOwnerWnd);
                InvalidateRect(g_hOwnerWnd, NULL, TRUE);
            }

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
        if (hBgBrush) DeleteObject(hBgBrush);
        if (hCardBrush) DeleteObject(hCardBrush);
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
    swc.lpszClassName = L"Windows11FluentSettingsGUI";
    swc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&swc);

    int winW = 396;
    int winH = 428;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    g_hSettingsWnd = CreateWindowExW(
        WS_EX_TOPMOST,
        swc.lpszClassName,
        L"Taskbar Monitor Settings",
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