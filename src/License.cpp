#include "License.h"
#include "Config.h"

// Button IDs
#define IDC_LIC_TEXT     1501
#define IDC_LIC_ACCEPT   1502
#define IDC_LIC_DECLINE  1503

static HWND g_hLicenseWnd = NULL;
static bool g_licenseAccepted = false;

static HFONT hLicTitleFont = NULL, hLicBodyFont = NULL, hLicBtnFont = NULL;
static HBRUSH hLicBgBrush = NULL, hLicCardBrush = NULL, hLicControlBrush = NULL;
static HBRUSH hLicAccentBrush = NULL;

static COLORREF cLicWindowBg    = RGB(26, 26, 28);
static COLORREF cLicCardBg      = RGB(36, 36, 40);
static COLORREF cLicControlBg   = RGB(46, 46, 52);
static COLORREF cLicBorder      = RGB(55, 55, 62);
static COLORREF cLicTextPrimary = RGB(245, 245, 248);
static COLORREF cLicTextSecondary = RGB(165, 170, 180);
static COLORREF cLicAccent      = RGB(0, 122, 220);

bool IsLicenseAccepted() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CONFIG_REGISTRY_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0, size = sizeof(DWORD);
        LONG res = RegQueryValueExW(hKey, LICENSE_REGISTRY_KEY, NULL, NULL, (LPBYTE)&val, &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS && val != 0);
    }
    return false;
}

void SetLicenseAccepted(bool accepted) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CONFIG_REGISTRY_KEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val = accepted ? 1 : 0;
        RegSetValueExW(hKey, LICENSE_REGISTRY_KEY, 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

static const wchar_t* kLicenseText =
    L"TaskbarMonitor \x2014 Release Edition 1.2.0\r\n"
    L"=================================================\r\n"
    L"\r\n"
    L"By running, installing, or using TaskbarMonitor you agree to the terms of\r\n"
    L"this license.\r\n"
    L"\r\n"
    L"MIT LICENSE\r\n"
    L"-----------\r\n"
    L"\r\n"
    L"Copyright (c) 2026 TaskbarMonitor Project\r\n"
    L"\r\n"
    L"Permission is hereby granted, free of charge, to any person obtaining a copy\r\n"
    L"of this software and associated documentation files (the \"Software\"), to\r\n"
    L"deal in the Software without restriction, including without limitation the\r\n"
    L"rights to use, copy, modify, merge, publish, distribute, sublicense, and/or\r\n"
    L"sell copies of the Software, and to permit persons to whom the Software is\r\n"
    L"furnished to do so, subject to the following conditions:\r\n"
    L"\r\n"
    L"The above copyright notice and this permission notice shall be included in\r\n"
    L"all copies or substantial portions of the Software.\r\n"
    L"\r\n"
    L"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\r\n"
    L"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\r\n"
    L"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\r\n"
    L"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\r\n"
    L"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\r\n"
    L"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN\r\n"
    L"THE SOFTWARE.\r\n"
    L"\r\n"
    L"NOTICE\r\n"
    L"-----\r\n"
    L"\r\n"
    L"TaskbarMonitor is provided free of charge. It collects no personal data and\r\n"
    L"sends nothing over the network. All configuration is stored locally in the\r\n"
    L"Windows registry under HKCU\\Software\\TaskbarMonitor.\r\n"
    L"\r\n"
    L"Windows may prompt you with an \"Unknown Publisher\" warning when you first\r\n"
    L"launch this unsigned build. That is a SmartScreen notice for executables that\r\n"
    L"have not been code-signed by a commercial certificate authority. TaskbarMonitor\r\n"
    L"is safe to run; choose \"More info\" and then \"Run anyway\" once.";

static LRESULT CALLBACK LicenseWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        DWORD cornerPref = 2;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        hLicTitleFont = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        hLicBodyFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
        hLicBtnFont = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

        hLicBgBrush      = CreateSolidBrush(cLicWindowBg);
        hLicCardBrush    = CreateSolidBrush(cLicCardBg);
        hLicControlBrush = CreateSolidBrush(cLicControlBg);
        hLicAccentBrush  = CreateSolidBrush(cLicAccent);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int clientW = rc.right - rc.left;
        int clientH = rc.bottom - rc.top;

        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", kLicenseText,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            24, 56, clientW - 48, clientH - 132,
            hWnd, (HMENU)IDC_LIC_TEXT, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Decline",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            clientW - 202, clientH - 56, 88, 36, hWnd, (HMENU)IDC_LIC_DECLINE, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"I Agree",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            clientW - 106, clientH - 56, 88, 36, hWnd, (HMENU)IDC_LIC_ACCEPT, NULL, NULL);

        HWND hText = GetDlgItem(hWnd, IDC_LIC_TEXT);
        if (hText) SendMessageW(hText, WM_SETFONT, (WPARAM)hLicBodyFont, TRUE);

        SetFocus(GetDlgItem(hWnd, IDC_LIC_ACCEPT));
        break;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, hLicBgBrush);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);

        HBRUSH headerBrush = hLicCardBrush;
        HPEN headerPen = CreatePen(PS_SOLID, 1, cLicBorder);
        RECT headerRc = { rc.left + 16, rc.top + 8, rc.right - 16, rc.top + 44 };
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, headerBrush);
        HGDIOBJ oldPen = SelectObject(hdc, headerPen);
        RoundRect(hdc, headerRc.left, headerRc.top, headerRc.right, headerRc.bottom, 8, 8);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(headerPen);

        RECT titleRc = headerRc;
        titleRc.left += 14;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, cLicTextPrimary);
        SelectObject(hdc, hLicTitleFont);
        DrawTextW(hdc, L"TaskbarMonitor \x2014 License Agreement", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT subRc = headerRc;
        subRc.left = rc.right - 200;
        SetTextColor(hdc, cLicTextSecondary);
        SelectObject(hdc, hLicBodyFont);
        DrawTextW(hdc, L"Release Edition 1.2.0", -1, &subRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC hdcEdit = (HDC)wParam;
        SetTextColor(hdcEdit, cLicTextPrimary);
        SetBkColor(hdcEdit, cLicControlBg);
        return (LRESULT)hLicControlBrush;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;

        if (dis->CtlID == IDC_LIC_ACCEPT || dis->CtlID == IDC_LIC_DECLINE) {
            HBRUSH fill = (dis->CtlID == IDC_LIC_ACCEPT)
                ? (dis->itemState & ODS_SELECTED ? CreateSolidBrush(RGB(0, 85, 160)) : CreateSolidBrush(cLicAccent))
                : (dis->itemState & ODS_SELECTED ? CreateSolidBrush(RGB(52, 52, 58)) : CreateSolidBrush(RGB(42, 42, 48)));
            FillRect(dis->hDC, &dis->rcItem, fill);
            DeleteObject(fill);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(255, 255, 255));
            SelectObject(dis->hDC, hLicBtnFont);
            DrawTextW(dis->hDC, dis->CtlID == IDC_LIC_ACCEPT ? L"I Agree" : L"Decline", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_LIC_ACCEPT) {
            g_licenseAccepted = true;
            SetLicenseAccepted(true);
            DestroyWindow(hWnd);
        } else if (id == IDC_LIC_DECLINE) {
            g_licenseAccepted = false;
            DestroyWindow(hWnd);
        }
        break;
    }

    case WM_CLOSE:
        g_licenseAccepted = false;
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        if (hLicTitleFont) DeleteObject(hLicTitleFont);
        if (hLicBodyFont) DeleteObject(hLicBodyFont);
        if (hLicBtnFont) DeleteObject(hLicBtnFont);
        if (hLicBgBrush) DeleteObject(hLicBgBrush);
        if (hLicCardBrush) DeleteObject(hLicCardBrush);
        if (hLicControlBrush) DeleteObject(hLicControlBrush);
        if (hLicAccentBrush) DeleteObject(hLicAccentBrush);
        g_hLicenseWnd = NULL;
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

bool ShowLicenseDialog(HINSTANCE hInstance) {
    if (IsLicenseAccepted()) return true;

    g_licenseAccepted = false;

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = LicenseWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TaskbarMonitorLicenseDLG";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    int winW = 640;
    int winH = 520;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;
    if (posX < 0) posX = 0;
    if (posY < 0) posY = 0;

    g_hLicenseWnd = CreateWindowExW(
        WS_EX_TOPMOST,
        wc.lpszClassName,
        L"Taskbar Monitor \x2014 License Agreement",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        posX, posY, winW, winH,
        NULL, NULL, hInstance, NULL
    );
    if (!g_hLicenseWnd) {
        UnregisterClassW(wc.lpszClassName, hInstance);
        return false;
    }

    ShowWindow(g_hLicenseWnd, SW_SHOW);
    UpdateWindow(g_hLicenseWnd);

    MSG msg;
    while (g_hLicenseWnd && IsWindow(g_hLicenseWnd)) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage((int)msg.wParam);
                g_hLicenseWnd = NULL;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_hLicenseWnd || !IsWindow(g_hLicenseWnd)) break;
        Sleep(10);
    }

    UnregisterClassW(wc.lpszClassName, hInstance);
    return g_licenseAccepted;
}