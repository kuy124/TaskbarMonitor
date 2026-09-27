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
UINT g_uTaskbarCreatedMsg = 0;
NOTIFYICONDATAW g_nid = { sizeof(NOTIFYICONDATAW) };

static void RefreshTrayTooltip() {
    if (!g_hWnd) return;
    swprintf(g_nid.szTip, _countof(g_nid.szTip), L"TaskbarMonitor | CPU %.0f%% | RAM %.0f%%",
             g_metrics.cpuUsage, g_metrics.memUsage);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_uTaskbarCreatedMsg) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        AttachToTaskbar(hWnd);
        InitTaskbarHooks(hWnd);
        SyncWithTaskbar(hWnd);
        return 0;
    }

    switch (msg) {
    case WM_CREATE: {
        LoadConfig();
        LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
        if (g_config.clickThrough) exStyle |= WS_EX_TRANSPARENT;
        else exStyle &= ~WS_EX_TRANSPARENT;
        SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle);
        g_curWidth = CalculateTotalWidth();
        UpdateThemeColors();
        InitMetrics();
        SetTimer(hWnd, TIMER_METRICS, g_config.refreshRateMs, NULL);
        AttachToTaskbar(hWnd);
        SyncWithTaskbar(hWnd);
        break;
    }

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
                DestroyWindow(hWnd);
            }
        } else if (lParam == WM_LBUTTONDBLCLK) {
            OpenSettingsWindow((HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), hWnd);
        }
        break;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        UpdateThemeColors();
        InvalidateRect(hWnd, NULL, FALSE);
        break;

    case WM_TIMER:
        if (wParam == TIMER_METRICS) {
            UpdateAllMetrics();
            RefreshTrayTooltip();
            InvalidateRect(hWnd, NULL, FALSE);
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
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        KillTimer(hWnd, TIMER_METRICS);
        CleanupTaskbarHooks();
        CleanupMetrics();
        CloseSettingsWindow();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
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

    DWORD style = g_hTaskbar ? (WS_CHILD | WS_CLIPSIBLINGS) : WS_POPUP;
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (g_config.clickThrough) {
        exStyle |= WS_EX_TRANSPARENT;
    }

    g_hWnd = CreateWindowExW(
        exStyle,
        wc.lpszClassName,
        L"TaskbarMonitor",
        style,
        0, 0, g_curWidth, MONITOR_HEIGHT,
        g_hTaskbar,
        NULL, hInstance, NULL
    );

    if (!g_hWnd) return 0;

    AttachToTaskbar(g_hWnd);
    SetLayeredWindowAttributes(g_hWnd, g_transparentKey, 0, LWA_COLORKEY);

    BOOL excludeFromPeek = TRUE;
    DwmSetWindowAttribute(g_hWnd, DWMWA_EXCLUDED_FROM_PEEK, &excludeFromPeek, sizeof(excludeFromPeek));

    // Tray Icon
    g_nid.hWnd = g_hWnd;
    g_nid.uID = 1001;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = wc.hIconSm;
    wcscpy_s(g_nid.szTip, L"TaskbarMonitor");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    InitTaskbarHooks(g_hWnd);
    UpdateAllMetrics();
    RefreshTrayTooltip();
    SyncWithTaskbar(g_hWnd);
    UpdateWindow(g_hWnd);

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
    return (int)msg.wParam;
}
