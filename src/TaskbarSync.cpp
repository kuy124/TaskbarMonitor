#include "TaskbarSync.h"
#include "Config.h"
#include "TaskbarPlacement.h"
#include <uiautomation.h>
#include <atomic>
#include <vector>

HWND g_hTaskbar = NULL;
static HWND g_hMainWnd = NULL;
static HWINEVENTHOOK g_hHookLoc = NULL;
static HWINEVENTHOOK g_hHookShow = NULL;
static HWINEVENTHOOK g_hHookHide = NULL;
static HWINEVENTHOOK g_hHookCreate = NULL;
static DWORD g_hookPid = 0;
static HWND g_hookTaskbar = NULL;
static std::atomic<bool> g_syncPending{false};
static UINT g_placementTimerMs = 0;
static IUIAutomation* g_uia = NULL;
static IUIAutomationElement* g_uiaRoot = NULL;
static bool g_comInitialized = false;

static TaskbarLayoutSnapshot g_layout;
static bool g_syncing = false;

static void RequestTaskbarSync() {
    // Explorer emits events during UIA queries and sibling reordering. Posting
    // a sync for each batch can starve WM_TIMER/WM_PAINT indefinitely.
    // The placement timer consumes this flag, at most once per tick.
    g_syncPending = true;
}

static void SetPlacementTimer(HWND hWnd, UINT intervalMs) {
    if (g_placementTimerMs != intervalMs) {
        SetTimer(hWnd, TIMER_PLACEMENT, intervalMs, NULL);
        g_placementTimerMs = intervalMs;
    }
}

static PlacementRect Bounds(const RECT& r) {
    return {r.left, r.top, r.right, r.bottom};
}

static bool IntersectsBand(const RECT& r, int y, int height) {
    return r.right > r.left && r.bottom > r.top &&
           r.bottom > y && r.top < y + height;
}

static void ReleaseUiaTaskbar() {
    if (g_uiaRoot) { g_uiaRoot->Release(); g_uiaRoot = NULL; }
}

static void BindUiaTaskbar() {
    ReleaseUiaTaskbar();
    if (!g_uia || !g_hTaskbar) return;
    HWND bridge = FindWindowExW(g_hTaskbar, NULL,
        L"Windows.UI.Composition.DesktopWindowContentBridge", NULL);
    if (bridge) g_uia->ElementFromHandle(bridge, &g_uiaRoot);
}

// UI Automation exposes XAML Search and app buttons that have no useful HWND bounds.
static bool AddAutomationButtons(int y, int height, int& trayLeft,
                                 std::vector<OccupiedSpan>& spans) {
    if (!g_uia || !g_uiaRoot) return false;
    VARIANT value = {};
    value.vt = VT_I4;
    value.lVal = UIA_ButtonControlTypeId;
    IUIAutomationCondition* condition = NULL;
    IUIAutomationElementArray* buttons = NULL;
    if (FAILED(g_uia->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &condition)))
        return false;
    HRESULT hr = g_uiaRoot->FindAll(TreeScope_Descendants, condition, &buttons);
    condition->Release();
    if (FAILED(hr) || !buttons) return false;
    int count = 0;
    hr = buttons->get_Length(&count);
    if (SUCCEEDED(hr) && count == 0) hr = E_FAIL;
    int visibleCount = 0;
    for (int i = 0; SUCCEEDED(hr) && i < count; ++i) {
        IUIAutomationElement* button = NULL;
        hr = buttons->GetElement(i, &button);
        if (FAILED(hr) || !button) break;
        BOOL offscreen = TRUE;
        RECT rect = {};
        BSTR className = NULL;
        HRESULT visibleHr = button->get_CurrentIsOffscreen(&offscreen);
        HRESULT rectHr = button->get_CurrentBoundingRectangle(&rect);
        HRESULT classHr = button->get_CurrentClassName(&className);
        button->Release();
        if (FAILED(visibleHr) || FAILED(rectHr) || FAILED(classHr)) {
            if (className) SysFreeString(className);
            hr = E_FAIL;
            break;
        }
        if (offscreen || !IntersectsBand(rect, y, height)) {
            if (className) SysFreeString(className);
            continue;
        }
        if (className && wcsncmp(className, L"SystemTray.", 11) == 0)
            trayLeft = std::min(trayLeft, static_cast<int>(rect.left));
        if (className) SysFreeString(className);
        ++visibleCount;
        MapWindowPoints(NULL, g_hTaskbar, reinterpret_cast<POINT*>(&rect), 2);
        spans.push_back({rect.left, rect.right});
    }
    buttons->Release();
    return SUCCEEDED(hr) && visibleCount > 0;
}

static bool GetOccupiedSpans(int y, int height, int& trayLeft,
                             std::vector<OccupiedSpan>& spans) {
    RECT client = {};
    GetClientRect(g_hTaskbar, &client);
    POINT origin = {0, 0};
    ClientToScreen(g_hTaskbar, &origin);
    int screenY = origin.y + y;
    trayLeft = origin.x + client.right;
    bool hasTray = false;
    bool hasButtons = false;
    bool hasComposition = false;
    bool unknownFullWidth = false;
    int buttonsLeft = client.right;
    std::vector<HWND> children;
    EnumChildWindows(g_hTaskbar, [](HWND child, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(data)->push_back(child);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&children));
    const wchar_t* shellClasses[] = {
        L"ReBarWindow32", L"TrayNotifyWnd", L"Start",
        L"TrayDummySearchControl",
        L"Windows.UI.Composition.DesktopWindowContentBridge"
    };
    for (const wchar_t* cls : shellClasses) {
        HWND child = FindWindowExW(g_hTaskbar, NULL, cls, NULL);
        if (child && std::find(children.begin(), children.end(), child) == children.end())
            children.push_back(child);
    }
    for (HWND child : children) {
        if (child == g_hMainWnd || IsChild(g_hMainWnd, child) ||
            !IsWindowVisible(child)) continue;
        RECT rect = {};
        if (!GetWindowRect(child, &rect) || !IntersectsBand(rect, screenY, height)) continue;
        wchar_t cls[96] = {};
        GetClassNameW(child, cls, _countof(cls));
        if (IsTaskbarBackgroundClass(cls)) {
            hasComposition = true;
            continue;
        }
        MapWindowPoints(NULL, g_hTaskbar, reinterpret_cast<POINT*>(&rect), 2);
        if (rect.right <= 0 || rect.left >= client.right) continue;
        if (wcscmp(cls, L"TrayNotifyWnd") == 0) {
            hasTray = true;
            trayLeft = origin.x + rect.left;
        }
        if (wcscmp(cls, L"ReBarWindow32") == 0 ||
            wcscmp(cls, L"MSTaskSwWClass") == 0) {
            hasButtons = true;
            buttonsLeft = std::min(buttonsLeft, static_cast<int>(rect.left));
        }
        if (rect.right - rect.left >= client.right - 8 &&
            wcscmp(cls, L"TrayNotifyWnd") != 0 &&
            wcscmp(cls, L"ReBarWindow32") != 0) unknownFullWidth = true;
        spans.push_back({rect.left, rect.right});
    }
    if (unknownFullWidth) return false;
    if (hasComposition && !g_uiaRoot) BindUiaTaskbar();
    bool automationReady = hasComposition &&
                           AddAutomationButtons(screenY, height, trayLeft, spans);
    if (hasComposition && !automationReady) ReleaseUiaTaskbar();
    if (automationReady) {
        trayLeft -= origin.x;
        return true;
    }
    trayLeft -= origin.x;
    if (!hasComposition && hasButtons && hasTray) {
        int safeLeft = std::max(0, buttonsLeft -
                                 static_cast<int>(client.bottom - client.top) - 8);
        spans.push_back({safeLeft, trayLeft});
        return true;
    }
    return false;
}

static int PreferredTaskbarX(int width, int trayLeft) {
    if (g_config.alignment == ALIGN_LEFT || g_config.alignment == ALIGN_CUSTOM)
        return g_config.offsetX;
    if (g_config.alignment == ALIGN_RIGHT)
        return trayLeft - g_curWidth - g_config.offsetX;
    return (width - g_curWidth) / 2 + g_config.offsetX;
}

static void EnsureTaskbarChild(HWND hWnd) {
    if (!g_hTaskbar || !IsWindow(g_hTaskbar)) return;
    if (GetParent(hWnd) != g_hTaskbar) {
        SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
        SetWindowLongPtrW(hWnd, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD | WS_CLIPSIBLINGS);
        SetParent(hWnd, g_hTaskbar);
        SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

static void PlaceWindow(HWND hWnd, int x, int y) {
    POINT screen = {x, y};
    ClientToScreen(g_hTaskbar, &screen);
    RECT current = {};
    GetWindowRect(hWnd, &current);
    if (IsWindowVisible(hWnd) && current.left == screen.x && current.top == screen.y &&
        current.right - current.left == g_curWidth &&
        current.bottom - current.top == g_monitorHeight) {
        if (GetWindow(hWnd, GW_HWNDPREV))
            SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    SetWindowPos(hWnd, HWND_TOP, x, y, g_curWidth, g_monitorHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void SyncWithTaskbar(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;
    // UI Automation can pump messages while calling Explorer.
    if (g_syncing) return;
    g_syncing = true;
    struct SyncGuard { ~SyncGuard() { g_syncing = false; } } guard;
    bool layoutChanged = g_syncPending.exchange(false);
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (taskbar != g_hTaskbar) {
        g_layout = {};
        g_hTaskbar = taskbar;
        InitTaskbarHooks(hWnd);
    }
    if (!g_hTaskbar || !IsWindowVisible(g_hTaskbar)) {
        ShowWindow(hWnd, SW_HIDE);
        SetPlacementTimer(hWnd, 100);
        return;
    }
    // Parent to the taskbar as soon as it exists. A child rides the taskbar
    // as it slides, so it needs no separate show and hide.
    EnsureTaskbarChild(hWnd);
    RECT taskbarRect = {}, client = {};
    HMONITOR taskbarMonitor = MonitorFromWindow(g_hTaskbar, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor = {sizeof(monitor)};
    if (!GetWindowRect(g_hTaskbar, &taskbarRect) ||
        !GetClientRect(g_hTaskbar, &client) ||
        !GetMonitorInfoW(taskbarMonitor, &monitor)) {
        ShowWindow(hWnd, SW_HIDE);
        SetPlacementTimer(hWnd, 100);
        return;
    }
    TaskbarEdge edge = DetectTaskbarEdge(Bounds(taskbarRect), Bounds(monitor.rcMonitor));
    int width = client.right - client.left;
    int height = client.bottom - client.top;
    bool horizontal = edge == TaskbarEdge::Bottom || edge == TaskbarEdge::Top;
    if (!horizontal) {
        // A side taskbar is a narrow strip; the readings cannot live in it.
        ShowWindow(hWnd, SW_HIDE);
        SetPlacementTimer(hWnd, 100);
        return;
    }
    // A real child window is covered by whatever covers the taskbar, so no
    // separate fullscreen test is needed.

    // Fill the taskbar band so the readings sit on the same surface as the
    // system clock, at whatever DPI and taskbar size the shell uses.
    if (g_monitorHeight != height) {
        g_monitorHeight = height;
        InvalidateRect(hWnd, NULL, FALSE);
    }
    int trayLeft = width;
    std::vector<OccupiedSpan> occupied;
    ULONGLONG now = GetTickCount64();
    bool cached = !layoutChanged && g_layout.CanReuse(width, height, now) &&
                  now - g_layout.measuredAt < 500;
    bool knownLayout = !cached && height >= g_monitorHeight &&
                       GetOccupiedSpans(0, g_monitorHeight, trayLeft, occupied);
    if (knownLayout) {
        g_layout = {occupied, width, height, trayLeft, now, true};
    } else if (g_layout.CanReuse(width, height, now)) {
        occupied = g_layout.spans;
        trayLeft = g_layout.trayLeft;
        knownLayout = true;
    }
    int preferredX = PreferredTaskbarX(width, trayLeft);
    std::optional<int> freeX = knownLayout
        ? FindFreeTaskbarPosition(width, g_curWidth, preferredX, occupied)
        : std::nullopt;
    // Prefer a clear gap. When the taskbar is crowded, keep the preferred spot
    // inside the taskbar instead of floating over the desktop.
    int targetX = freeX ? *freeX : std::clamp(preferredX, 0, std::max(0, width - g_curWidth));
    PlaceWindow(hWnd, targetX, 0);
    SetPlacementTimer(hWnd, 100);
}

void TickTaskbarPlacement(HWND hWnd) { SyncWithTaskbar(hWnd); }

static bool IsTaskbarOrChild(HWND hwnd) {
    if (!hwnd || !g_hTaskbar) return false;
    if (g_hMainWnd && (hwnd == g_hMainWnd || IsChild(g_hMainWnd, hwnd))) return false;
    return hwnd == g_hTaskbar || IsChild(g_hTaskbar, hwnd);
}

static void CALLBACK LocationWinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd,
                                           LONG, LONG, DWORD, DWORD) {
    if (IsTaskbarOrChild(hwnd)) RequestTaskbarSync();
}

static void CALLBACK ShowHideWinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd,
                                           LONG, LONG, DWORD, DWORD) {
    if (IsTaskbarOrChild(hwnd)) RequestTaskbarSync();
}

static void CALLBACK CreateWinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd,
                                         LONG, LONG, DWORD, DWORD) {
    if (!hwnd || !IsWindow(hwnd)) return;
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, _countof(cls));
    if (wcscmp(cls, L"Shell_TrayWnd") == 0) RequestTaskbarSync();
}

void InitTaskbarHooks(HWND hWnd) {
    g_hMainWnd = hWnd;
    g_hTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!g_comInitialized) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        g_comInitialized = SUCCEEDED(hr);
    }
    if (!g_uia && g_comInitialized)
        CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                         IID_IUIAutomation, reinterpret_cast<void**>(&g_uia));
    if (g_hTaskbar) {
        DWORD explorerPid = 0;
        GetWindowThreadProcessId(g_hTaskbar, &explorerPid);
        if (explorerPid && (explorerPid != g_hookPid || g_hTaskbar != g_hookTaskbar)) {
            ReleaseUiaTaskbar();
            if (g_hHookLoc) UnhookWinEvent(g_hHookLoc);
            if (g_hHookShow) UnhookWinEvent(g_hHookShow);
            if (g_hHookHide) UnhookWinEvent(g_hHookHide);
            g_hookPid = explorerPid;
            g_hookTaskbar = g_hTaskbar;
            g_hHookLoc = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,
                EVENT_OBJECT_LOCATIONCHANGE, NULL, LocationWinEventProc,
                explorerPid, 0, WINEVENT_OUTOFCONTEXT);
            g_hHookShow = SetWinEventHook(EVENT_OBJECT_SHOW,
                EVENT_OBJECT_SHOW, NULL, ShowHideWinEventProc,
                explorerPid, 0, WINEVENT_OUTOFCONTEXT);
            g_hHookHide = SetWinEventHook(EVENT_OBJECT_HIDE,
                EVENT_OBJECT_HIDE, NULL, ShowHideWinEventProc,
                explorerPid, 0, WINEVENT_OUTOFCONTEXT);
            BindUiaTaskbar();
        }
    }
    if (!g_hHookCreate)
        g_hHookCreate = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE,
            NULL, CreateWinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
}

void CleanupTaskbarHooks() {
    g_hMainWnd = NULL;
    g_placementTimerMs = 0;
    g_layout = {};
    ReleaseUiaTaskbar();
    if (g_uia) { g_uia->Release(); g_uia = NULL; }
    if (g_comInitialized) { CoUninitialize(); g_comInitialized = false; }
    if (g_hHookLoc) { UnhookWinEvent(g_hHookLoc); g_hHookLoc = NULL; }
    if (g_hHookShow) { UnhookWinEvent(g_hHookShow); g_hHookShow = NULL; }
    if (g_hHookHide) { UnhookWinEvent(g_hHookHide); g_hHookHide = NULL; }
    if (g_hHookCreate) { UnhookWinEvent(g_hHookCreate); g_hHookCreate = NULL; }
    g_hookPid = 0;
    g_hookTaskbar = NULL;
    g_syncPending = false;
}
