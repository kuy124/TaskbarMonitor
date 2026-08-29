#include "TaskbarSync.h"
#include "Config.h"

HWND g_hTaskbar = NULL;
static HWINEVENTHOOK g_hHookFore = NULL;
static HWINEVENTHOOK g_hHookLoc = NULL;
static HWND g_hMainWnd = NULL;

void SyncWithTaskbar(HWND hWnd) {
    if (!g_hTaskbar || !IsWindow(g_hTaskbar)) {
        g_hTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);
        if (!g_hTaskbar) return;
        SetWindowLongPtr(hWnd, GWLP_HWNDPARENT, (LONG_PTR)g_hTaskbar);
    }

    RECT tbRect = {0};
    if (!GetWindowRect(g_hTaskbar, &tbRect)) return;

    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int screenW = GetSystemMetrics(SM_CXSCREEN);

    if (tbRect.bottom - tbRect.top <= 4 || tbRect.right - tbRect.left <= 4 ||
        tbRect.top >= screenH - 3 || tbRect.bottom <= 3) {
        ShowWindow(hWnd, SW_HIDE);
        return;
    }

    int taskbarHeight = tbRect.bottom - tbRect.top;
    int taskbarWidth = tbRect.right - tbRect.left;
    int xPos = 0;
    int yPos = 0;

    if (taskbarWidth > taskbarHeight) {
        xPos = tbRect.left + g_config.leftMargin;
        yPos = tbRect.top + (taskbarHeight - MONITOR_HEIGHT) / 2;
    } else {
        xPos = tbRect.left + (taskbarWidth - g_curWidth) / 2;
        yPos = tbRect.top + g_config.leftMargin;
    }

    SetWindowPos(
        hWnd,
        HWND_TOPMOST,
        xPos, yPos, g_curWidth, MONITOR_HEIGHT,
        SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS
    );
}

static VOID CALLBACK LocationWinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG, LONG, DWORD, DWORD) {
    if (hwnd == g_hTaskbar && g_hMainWnd && IsWindow(g_hMainWnd)) {
        SyncWithTaskbar(g_hMainWnd);
    }
}

static VOID CALLBACK ForegroundWinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (g_hMainWnd && IsWindow(g_hMainWnd)) {
        SyncWithTaskbar(g_hMainWnd);
    }
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
        }
    }

    g_hHookFore = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        NULL,
        ForegroundWinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT
    );
}

void CleanupTaskbarHooks() {
    if (g_hHookFore) { UnhookWinEvent(g_hHookFore); g_hHookFore = NULL; }
    if (g_hHookLoc)  { UnhookWinEvent(g_hHookLoc);  g_hHookLoc = NULL; }
}