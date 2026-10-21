#include "Common.h"
#include "Config.h"
#include <wchar.h>
#include "Theme.h"
#include "Metrics.h"
#include "Renderer.h"
#include "TaskbarSync.h"
#include "SettingsWindow.h"
#include "StartupDialog.h"
#include "License.h"
#include "Resource.h"

HWND g_hWnd = NULL;
static HWND g_hHostWnd = NULL;
static bool g_initialized = false;
UINT g_uTaskbarCreatedMsg = 0;
NOTIFYICONDATAW g_nid = { sizeof(NOTIFYICONDATAW) };

static void CreateMonitorWindow(HINSTANCE hInstance);
static void RecoverMonitorWindow() {
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!taskbar) return;
    if (g_hWnd && IsWindow(g_hWnd)) {
        if (IsAttachedToTaskbar(g_hWnd, taskbar)) return;
        // A window that is still a popup can join the taskbar, and the sync
        // does that. Only a window that was orphaned by a taskbar that has
        // been replaced has to be replaced: a child of another process
        // cannot be moved to a different parent.
        if (!(GetWindowLongPtrW(g_hWnd, GWL_STYLE) & WS_CHILD)) return;
    }
    // Explorer replaces its taskbar now and then, and the replacement can
    // reuse the handle of the window it replaces. The monitor window is a
    // child of another process, so it survives that replacement as an orphan
    // of the dead taskbar: the handle still matches, but nothing is drawn.
    // Only the taskbar knows which children it really has, so ask it and
    // build a fresh window for the taskbar that is there now.
    if (g_hWnd && IsWindow(g_hWnd)) DestroyWindow(g_hWnd);
    g_hWnd = NULL;
    CleanupTaskbarHooks();
    CreateMonitorWindow((HINSTANCE)GetWindowLongPtrW(g_hHostWnd, GWLP_HINSTANCE));
}

static LRESULT CALLBACK HostWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_uTaskbarCreatedMsg) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        RecoverMonitorWindow();
        if (g_hWnd) {
            g_hTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);
            InitTaskbarHooks(g_hWnd);
            SyncWithTaskbar(g_hWnd);
        }
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        SetTimer(hWnd, 3, 200, NULL);
        return 0;
    case WM_TIMER:
        if (wParam == 3) RecoverMonitorWindow();
        return 0;
    case WM_TRAYICON:
        RecoverMonitorWindow();
        if (g_hWnd) SendMessageW(g_hWnd, msg, wParam, lParam);
        return 0;
    case WM_DESTROY:
        KillTimer(hWnd, 3);
        if (g_hWnd && IsWindow(g_hWnd)) DestroyWindow(g_hWnd);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        CleanupTaskbarHooks();
        CleanupMetrics();
        CloseSettingsWindow();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

static void RefreshTrayTooltip() {
    if (!g_hWnd) return;
    swprintf(g_nid.szTip, _countof(g_nid.szTip), L"TaskbarMonitor | CPU %.0f%% | RAM %.0f%%",
             g_metrics.cpuUsage, g_metrics.memUsage);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        if (!g_initialized) LoadConfig();
        LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
        if (g_config.clickThrough) exStyle |= WS_EX_TRANSPARENT;
        else exStyle &= ~WS_EX_TRANSPARENT;
        SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle);
        g_curWidth = CalculateTotalWidth();
        UpdateThemeColors();
        if (!g_initialized) {
            InitMetrics();
            g_initialized = true;
        }
        SetTimer(hWnd, TIMER_METRICS, g_config.refreshRateMs, NULL);
        break;
    }

    case WM_NCHITTEST:
        // A child of the taskbar still swallows clicks over its own area.
        // Returning HTTRANSPARENT lets them reach the taskbar underneath.
        if (g_config.clickThrough) return HTTRANSPARENT;
        return DefWindowProcW(hWnd, msg, wParam, lParam);

    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN:
        if (!g_config.clickThrough) {
            OpenSettingsWindow((HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), hWnd);
        }
        break;

    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"Settings...");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_TASKMGR,  L"Open Task Manager");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_REFRESH,  L"Refresh Theme");
            AppendMenuW(hMenu, MF_STRING | (g_config.clickThrough ? MF_CHECKED : 0), ID_TRAY_CLICKTHROUGH, L"Click-Through Mode");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT,     L"Exit Monitor");

            SetForegroundWindow(hWnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);

            if (cmd == ID_TRAY_SETTINGS) {
                OpenSettingsWindow((HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), hWnd);
            } else if (cmd == ID_TRAY_TASKMGR) {
                ShellExecuteW(NULL, L"open", L"taskmgr.exe", NULL, NULL, SW_SHOW);
            } else if (cmd == ID_TRAY_REFRESH) {
                UpdateThemeColors();
                SyncWithTaskbar(hWnd);
                InvalidateRect(hWnd, NULL, FALSE);
            } else if (cmd == ID_TRAY_CLICKTHROUGH) {
                g_config.clickThrough = !g_config.clickThrough;
                LONG_PTR exStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
                if (g_config.clickThrough) exStyle |= WS_EX_TRANSPARENT;
                else exStyle &= ~WS_EX_TRANSPARENT;
                SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle);
                SaveConfig();
            } else if (cmd == ID_TRAY_EXIT) {
                DestroyWindow(g_hHostWnd);
            }
        } else if (lParam == WM_LBUTTONDBLCLK) {
            OpenSettingsWindow((HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), hWnd);
        }
        break;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        UpdateThemeColors();
        SyncWithTaskbar(hWnd);
        InvalidateRect(hWnd, NULL, FALSE);
        break;

    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
    case WM_TASKBAR_SYNC:
        SyncWithTaskbar(hWnd);
        break;

    case WM_TIMER:
        if (wParam == TIMER_METRICS) {
            UpdateAllMetrics();
            RefreshTrayTooltip();
            InvalidateRect(hWnd, NULL, FALSE);
        } else if (wParam == TIMER_PLACEMENT) {
            TickTaskbarPlacement(hWnd);
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RenderOverlay(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_METRICS);
        KillTimer(hWnd, TIMER_PLACEMENT);
        CloseSettingsWindow();
        CleanupTaskbarHooks();
        if (g_hWnd == hWnd) g_hWnd = NULL;
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static void CreateMonitorWindow(HINSTANCE hInstance) {
    if (g_hWnd && IsWindow(g_hWnd)) return;
    // The window is a plain popup on purpose. A window that is created with
    // a window of another process as its parent keeps that window as its
    // owner, and the shell then refuses to take it as a child.
    g_hWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"TaskbarMonitor", L"TaskbarMonitor", WS_POPUP,
        0, 0, g_curWidth, g_monitorHeight, NULL, NULL, hInstance, NULL);
    if (!g_hWnd) return;
    g_hTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    SetLayeredWindowAttributes(g_hWnd, g_transparentKey, 0, LWA_COLORKEY);
    BOOL excludeFromPeek = TRUE;
    DwmSetWindowAttribute(g_hWnd, DWMWA_EXCLUDED_FROM_PEEK,
                          &excludeFromPeek, sizeof(excludeFromPeek));
    InitTaskbarHooks(g_hWnd);
    UpdateAllMetrics();
    HDC initialDC = GetDC(g_hWnd);
    if (initialDC) {
        RenderOverlay(g_hWnd, initialDC);
        ReleaseDC(g_hWnd, initialDC);
    }
    SyncWithTaskbar(g_hWnd);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int) {
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icex);

    if (!ShowLicenseDialog(hInstance)) {
        return 0;
    }

    g_uTaskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    g_hTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TaskbarMonitor";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_TASKBARMONITOR));
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_TASKBARMONITOR),
                                  IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    RegisterClassExW(&wc);

    WNDCLASSEXW hostClass = { sizeof(WNDCLASSEXW) };
    hostClass.lpfnWndProc = HostWndProc;
    hostClass.hInstance = hInstance;
    hostClass.lpszClassName = L"TaskbarMonitorHost";
    RegisterClassExW(&hostClass);
    g_hHostWnd = CreateWindowExW(WS_EX_TOOLWINDOW, hostClass.lpszClassName,
                                  L"TaskbarMonitorHost", WS_POPUP,
                                  0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!g_hHostWnd) return 0;
    CreateMonitorWindow(hInstance);
    if (!g_hWnd) {
        DestroyWindow(g_hHostWnd);
        return 0;
    }

    // Tray Icon
    g_nid.hWnd = g_hHostWnd;
    g_nid.uID = 1001;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = wc.hIconSm;
    wcscpy_s(g_nid.szTip, L"TaskbarMonitor");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    RefreshTrayTooltip();

    bool openSettings = (pCmdLine && wcsstr(pCmdLine, L"--settings") != NULL);
    bool bAutoStart = (pCmdLine && wcsstr(pCmdLine, L"--autostart") != NULL);
    if (openSettings) {
        OpenSettingsWindow(hInstance, g_hWnd);
    } else if (!bAutoStart) {
        AutostartInfo autostartInfo = QueryAutostartStatus();
        if (autostartInfo.status != AUTOSTART_ENABLED) {
            ShowAutostartDialog(hInstance, g_hWnd, autostartInfo);
            g_config.runAtStartup = IsAutostartEnabled();
        }
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (g_hSettingsWnd && IsWindow(g_hSettingsWnd) && IsDialogMessageW(g_hSettingsWnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ShutdownTaskbarHooks();
    return (int)msg.wParam;
}
