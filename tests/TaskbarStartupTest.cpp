#include "Config.h"
#include "TaskbarSync.h"
#include "Theme.h"
#include <cassert>

MonitorConfig g_config = {};
int g_curWidth = 160;
int g_monitorHeight = 36;
COLORREF g_transparentKey = RGB(1, 1, 1);

int main() {
    // A separate desktop lets Explorer start late without touching the user's shell.
    HDESK original = GetThreadDesktop(GetCurrentThreadId());
    wchar_t name[64];
    swprintf_s(name, L"TaskbarStartupTest-%lu", GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name, NULL, NULL, 0, GENERIC_ALL, NULL);
    assert(desktop && SetThreadDesktop(desktop));

    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = L"Shell_TrayWnd";
    assert(RegisterClassW(&wc));

    HWND monitor = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC", L"Monitor", WS_POPUP, 0, 0, g_curWidth, g_monitorHeight,
        NULL, NULL, instance, NULL);
    assert(monitor);
    // Simulate a failed initial transparency setup before Explorer is available.
    SyncWithTaskbar(monitor);
    assert(!IsWindowVisible(monitor));

    MONITORINFO info = {sizeof(info)};
    assert(GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info));
    HWND taskbar = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Taskbar",
        WS_POPUP, info.rcMonitor.left, info.rcMonitor.bottom - 48,
        info.rcMonitor.right - info.rcMonitor.left, 48,
        NULL, NULL, instance, NULL);
    assert(taskbar);
    TickTaskbarPlacement(monitor);
    assert(!IsWindowVisible(monitor));

    ShowWindow(taskbar, SW_SHOWNOACTIVATE);
    TickTaskbarPlacement(monitor);
    assert(IsAttachedToTaskbar(monitor, taskbar));
    assert(IsWindowVisible(monitor));
    COLORREF key = 0;
    BYTE alpha = 0;
    DWORD flags = 0;
    assert(GetLayeredWindowAttributes(monitor, &key, &alpha, &flags));
    assert(key == g_transparentKey && flags == LWA_COLORKEY);

    // Losing the layered surface after attachment must also recover without Save.
    LONG_PTR exStyle = GetWindowLongPtrW(monitor, GWL_EXSTYLE);
    SetWindowLongPtrW(monitor, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
    SetWindowLongPtrW(monitor, GWL_EXSTYLE, exStyle);
    assert(!GetLayeredWindowAttributes(monitor, &key, &alpha, &flags));
    TickTaskbarPlacement(monitor);
    assert(GetLayeredWindowAttributes(monitor, &key, &alpha, &flags));
    assert(key == g_transparentKey && flags == LWA_COLORKEY);
    assert(IsWindowVisible(monitor));

    CleanupTaskbarHooks();
    ShutdownTaskbarHooks();
    DestroyWindow(monitor);
    DestroyWindow(taskbar);
    assert(SetThreadDesktop(original));
    assert(CloseDesktop(desktop));
}
