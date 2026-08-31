#include "TaskbarSync.h"
#include "Config.h"

HWND g_hTaskbar = NULL;
static HWINEVENTHOOK g_hHookLoc = NULL;
static HWINEVENTHOOK g_hHookShow = NULL;
static HWINEVENTHOOK g_hHookHide = NULL;
static HWINEVENTHOOK g_hHookCreate = NULL;
static HWND g_hMainWnd = NULL;

void AttachToTaskbar(HWND hWnd) {
    g_hTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!g_hTaskbar || !IsWindow(g_hTaskbar)) return;

    if (GetParent(hWnd) != g_hTaskbar) {
        LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
        style &= ~WS_POPUP;
        style |= WS_CHILD | WS_CLIPSIBLINGS;
        SetWindowLongPtrW(hWnd, GWL_STYLE, style);

        LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
        exStyle &= ~WS_EX_TOPMOST;
        exStyle |= WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        if (g_config.clickThrough) {
            exStyle |= WS_EX_TRANSPARENT;
        }
        SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle);

        SetParent(hWnd, g_hTaskbar);
    }
}

void SyncWithTaskbar(HWND hWnd) {
    if (!g_hTaskbar || !IsWindow(g_hTaskbar)) {
        AttachToTaskbar(hWnd);
        if (!g_hTaskbar) return;
    }

    if (GetParent(hWnd) != g_hTaskbar) {
        AttachToTaskbar(hWnd);
    }

    RECT tbClientRect = {0};
    if (!GetClientRect(g_hTaskbar, &tbClientRect)) return;

    int tbWidth = tbClientRect.right - tbClientRect.left;
    int tbHeight = tbClientRect.bottom - tbClientRect.top;

    if (tbWidth <= 4 || tbHeight <= 4) {
        ShowWindow(hWnd, SW_HIDE);
        return;
    }

    int xPos = 0;
    int yPos = (tbHeight - MONITOR_HEIGHT) / 2 + g_config.offsetY;

    if (g_config.alignment == ALIGN_LEFT) {
        xPos = g_config.offsetX;
    } else if (g_config.alignment == ALIGN_RIGHT) {
        HWND hTray = FindWindowExW(g_hTaskbar, NULL, L"TrayNotifyWnd", NULL);
        if (hTray && IsWindowVisible(hTray)) {
            RECT trayRect = {0};
            GetWindowRect(hTray, &trayRect);
            POINT ptTray = { trayRect.left, trayRect.top };
            ScreenToClient(g_hTaskbar, &ptTray);
            xPos = ptTray.x - g_curWidth - g_config.offsetX;
        } else {
            xPos = tbWidth - g_curWidth - g_config.offsetX;
        }
    } else if (g_config.alignment == ALIGN_CENTER) {
        xPos = (tbWidth - g_curWidth) / 2 + g_config.offsetX;
    } else { // ALIGN_CUSTOM
        xPos = g_config.offsetX;
        yPos = g_config.offsetY;
    }

    if (xPos < 0) xPos = 0;
    if (xPos + g_curWidth > tbWidth) xPos = tbWidth - g_curWidth;
    if (yPos < 0) yPos = 0;
    if (yPos + MONITOR_HEIGHT > tbHeight) yPos = tbHeight - MONITOR_HEIGHT;

    SetWindowPos(hWnd, HWND_TOP, xPos, yPos, g_curWidth, MONITOR_HEIGHT,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static bool IsTaskbarOrChild(HWND hwnd) {
    if (!hwnd || !g_hTaskbar) return false;
    if (hwnd == g_hTaskbar) return true;
    return IsChild(g_hTaskbar, hwnd);
}

static void CALLBACK LocationWinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG, LONG, DWORD, DWORD) {
    if (IsTaskbarOrChild(hwnd) && g_hMainWnd && IsWindow(g_hMainWnd)) {
        SyncWithTaskbar(g_hMainWnd);
    }
}

static void CALLBACK ShowHideWinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG, LONG, DWORD, DWORD) {
    if (!g_hMainWnd || !IsWindow(g_hMainWnd)) return;
    if (IsTaskbarOrChild(hwnd)) {
        SyncWithTaskbar(g_hMainWnd);
    }
}

static void CALLBACK CreateWinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG, LONG, DWORD, DWORD) {
    if (!g_hMainWnd || !IsWindow(g_hMainWnd)) return;
    if (hwnd && IsWindow(hwnd)) {
        wchar_t cls[64] = {0};
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"Shell_TrayWnd") == 0) {
            AttachToTaskbar(g_hMainWnd);
        }
    }
    SyncWithTaskbar(g_hMainWnd);
}

void InitTaskbarHooks(HWND hWnd) {
    g_hMainWnd = hWnd;
    g_hTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);

    if (g_hTaskbar) {
        DWORD explorerPid = 0;
        GetWindowThreadProcessId(g_hTaskbar, &explorerPid);
        if (explorerPid != 0) {
            g_hHookLoc = SetWinEventHook(
                EVENT_OBJECT_LOCATIONCHANGE,
                EVENT_OBJECT_LOCATIONCHANGE,
                NULL,
                LocationWinEventProc,
                explorerPid,
                0,
                WINEVENT_OUTOFCONTEXT
            );

            g_hHookShow = SetWinEventHook(
                EVENT_OBJECT_SHOW,
                EVENT_OBJECT_SHOW,
                NULL,
                ShowHideWinEventProc,
                explorerPid,
                0,
                WINEVENT_OUTOFCONTEXT
            );

            g_hHookHide = SetWinEventHook(
                EVENT_OBJECT_HIDE,
                EVENT_OBJECT_HIDE,
                NULL,
                ShowHideWinEventProc,
                explorerPid,
                0,
                WINEVENT_OUTOFCONTEXT
            );
        }
    }

    g_hHookCreate = SetWinEventHook(
        EVENT_OBJECT_CREATE,
        EVENT_OBJECT_CREATE,
        NULL,
        CreateWinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT
    );
}

void CleanupTaskbarHooks() {
    if (g_hHookLoc)    { UnhookWinEvent(g_hHookLoc);    g_hHookLoc = NULL; }
    if (g_hHookShow)   { UnhookWinEvent(g_hHookShow);   g_hHookShow = NULL; }
    if (g_hHookHide)   { UnhookWinEvent(g_hHookHide);   g_hHookHide = NULL; }
    if (g_hHookCreate) { UnhookWinEvent(g_hHookCreate); g_hHookCreate = NULL; }
}