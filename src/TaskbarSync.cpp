#include "TaskbarSync.h"
#include "Config.h"
#include "TaskbarPlacement.h"
#include <uiautomation.h>
#include <atomic>
#include <vector>

HWND g_hTaskbar = NULL;
static std::atomic<HWND> g_hMainWnd{nullptr};
static HWINEVENTHOOK g_hHookLoc = NULL;
static HWINEVENTHOOK g_hHookShow = NULL;
static HWINEVENTHOOK g_hHookHide = NULL;
static HWINEVENTHOOK g_hHookCreate = NULL;
static DWORD g_hookPid = 0;
static HWND g_hookTaskbar = NULL;
static std::atomic<bool> g_syncPending{false};
static UINT g_placementTimerMs = 0;
static bool g_syncing = false;

// Measuring the taskbar needs UI Automation, which blocks for tens to hundreds
// of milliseconds. On the UI thread that froze the settings window and every
// other window this app owns, so the measurement runs on a worker thread and
// publishes a snapshot that the UI thread only reads.
static SRWLOCK g_layoutLock = SRWLOCK_INIT;
static TaskbarLayoutSnapshot g_layout;
static HANDLE g_scanThread = NULL;
static HANDLE g_scanEvent = NULL;
static std::atomic<bool> g_scanStop{false};

// Worker-thread-only UI Automation state.
static IUIAutomation* g_uia = NULL;
static IUIAutomationElement* g_uiaRoot = NULL;
static HWND g_uiaTaskbar = NULL;

static void RequestTaskbarSync() {
    // Explorer emits events during UIA queries and sibling reordering. Posting
    // a sync for each batch can starve WM_TIMER/WM_PAINT indefinitely.
    // The placement timer consumes this flag, at most once per tick.
    g_syncPending = true;
}

static void RequestScan() {
    if (g_scanEvent) SetEvent(g_scanEvent);
}

static void SetPlacementTimer(HWND hWnd, int intervalMs) {
    if (g_placementTimerMs != (UINT)intervalMs) {
        SetTimer(hWnd, TIMER_PLACEMENT, intervalMs, NULL);
        g_placementTimerMs = (UINT)intervalMs;
    }
}

static PlacementRect Bounds(const RECT& r) {
    return {r.left, r.top, r.right, r.bottom};
}

static bool IntersectsBand(const RECT& r, int y, int height) {
    return r.right > r.left && r.bottom > r.top &&
           r.bottom > y && r.top < y + height;
}

// --- Worker thread -----------------------------------------------------------
// Everything below runs on the scan worker, never on the UI thread.

static void ReleaseUiaTaskbar() {
    if (g_uiaRoot) { g_uiaRoot->Release(); g_uiaRoot = NULL; }
    g_uiaTaskbar = NULL;
}

static void BindUiaTaskbar(HWND taskbar) {
    if (g_uiaTaskbar == taskbar && g_uiaRoot) return;
    ReleaseUiaTaskbar();
    if (!g_uia || !taskbar) return;
    HWND bridge = FindWindowExW(taskbar, NULL,
        L"Windows.UI.Composition.DesktopWindowContentBridge", NULL);
    if (bridge) {
        g_uia->ElementFromHandle(bridge, &g_uiaRoot);
        g_uiaTaskbar = taskbar;
    }
}

// UI Automation exposes XAML Search and app buttons that have no useful HWND bounds.
static bool AddAutomationButtons(HWND taskbar, int y, int height, int& trayLeft,
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
        MapWindowPoints(NULL, taskbar, reinterpret_cast<POINT*>(&rect), 2);
        spans.push_back({rect.left, rect.right});
    }
    buttons->Release();
    return SUCCEEDED(hr) && visibleCount > 0;
}

static bool MeasureTaskbar(HWND taskbar, int& trayLeft,
                            std::vector<OccupiedSpan>& spans) {
    RECT client = {};
    GetClientRect(taskbar, &client);
    POINT origin = {0, 0};
    ClientToScreen(taskbar, &origin);
    int screenY = origin.y;
    int height = client.bottom - client.top;
    trayLeft = origin.x + client.right;
    bool hasTray = false;
    bool hasButtons = false;
    bool hasComposition = false;
    bool unknownFullWidth = false;
    int buttonsLeft = client.right;
    std::vector<HWND> children;
    EnumChildWindows(taskbar, [](HWND child, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(data)->push_back(child);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&children));
    const wchar_t* shellClasses[] = {
        L"ReBarWindow32", L"TrayNotifyWnd", L"Start",
        L"TrayDummySearchControl",
        L"Windows.UI.Composition.DesktopWindowContentBridge"
    };
    for (const wchar_t* cls : shellClasses) {
        HWND child = FindWindowExW(taskbar, NULL, cls, NULL);
        if (child && std::find(children.begin(), children.end(), child) == children.end())
            children.push_back(child);
    }
    for (HWND child : children) {
        HWND mainWnd = g_hMainWnd.load();
        if (child == mainWnd || IsChild(mainWnd, child) ||
            !IsWindowVisible(child)) continue;
        RECT rect = {};
        if (!GetWindowRect(child, &rect) || !IntersectsBand(rect, screenY, height)) continue;
        wchar_t cls[96] = {};
        GetClassNameW(child, cls, _countof(cls));
        if (IsTaskbarBackgroundClass(cls)) {
            hasComposition = true;
            continue;
        }
        MapWindowPoints(NULL, taskbar, reinterpret_cast<POINT*>(&rect), 2);
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
    if (hasComposition) BindUiaTaskbar(taskbar);
    bool automationReady = hasComposition &&
                           AddAutomationButtons(taskbar, screenY, height, trayLeft, spans);
    if (hasComposition && !automationReady) ReleaseUiaTaskbar();
    if (automationReady) {
        trayLeft -= origin.x;
        return true;
    }
    trayLeft -= origin.x;
    if (!hasComposition && hasButtons && hasTray) {
        int safeLeft = std::max(0, buttonsLeft - height - 8);
        spans.push_back({safeLeft, trayLeft});
        return true;
    }
    return false;
}

static DWORD WINAPI ScanThread(LPVOID) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                     IID_IUIAutomation, reinterpret_cast<void**>(&g_uia));
    ULONGLONG lastScan = 0;
    while (!g_scanStop.load()) {
        // Wait for a request, but refresh on our own at least once a second so
        // the snapshot never goes stale even if no shell event arrives.
        if (WaitForSingleObject(g_scanEvent, 1000) == WAIT_FAILED) break;
        if (g_scanStop.load()) break;
        ULONGLONG now = GetTickCount64();
        // Explorer's own events fire during a query, so coalesce bursts.
        if (lastScan && now - lastScan < 250) continue;
        lastScan = now;
        HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
        if (!taskbar || !IsWindow(taskbar)) continue;
        std::vector<OccupiedSpan> spans;
        int trayLeft = 0;
        if (!MeasureTaskbar(taskbar, trayLeft, spans)) continue;
        RECT client = {};
        GetClientRect(taskbar, &client);
        TaskbarLayoutSnapshot snapshot;
        snapshot.spans = std::move(spans);
        snapshot.width = client.right - client.left;
        snapshot.height = client.bottom - client.top;
        snapshot.trayLeft = trayLeft;
        snapshot.measuredAt = GetTickCount64();
        snapshot.valid = true;
        {
            AcquireSRWLockExclusive(&g_layoutLock);
            g_layout = std::move(snapshot);
            ReleaseSRWLockExclusive(&g_layoutLock);
        }
    }
    ReleaseUiaTaskbar();
    if (g_uia) { g_uia->Release(); g_uia = NULL; }
    CoUninitialize();
    return 0;
}

static void StartScanThread() {
    if (g_scanThread) return;
    g_scanStop = false;
    g_scanEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!g_scanEvent) return;
    g_scanThread = CreateThread(NULL, 0, ScanThread, NULL, 0, NULL);
    if (!g_scanThread) {
        CloseHandle(g_scanEvent);
        g_scanEvent = NULL;
    }
}

// --- UI thread ---------------------------------------------------------------

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
    if (g_syncing) return;
    g_syncing = true;
    struct SyncGuard { ~SyncGuard() { g_syncing = false; } } guard;
    bool layoutChanged = g_syncPending.exchange(false);
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (taskbar != g_hTaskbar) {
        {
            AcquireSRWLockExclusive(&g_layoutLock);
            g_layout = {};
            ReleaseSRWLockExclusive(&g_layoutLock);
        }
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
    // The measurement itself happens on the worker thread, so this never blocks.
    // Ask for a fresh scan when the shell reported a layout change; otherwise
    // the worker refreshes the snapshot on its own.
    if (layoutChanged) RequestScan();
    TaskbarLayoutSnapshot snapshot;
    {
        AcquireSRWLockShared(&g_layoutLock);
        snapshot = g_layout;
        ReleaseSRWLockShared(&g_layoutLock);
    }
    ULONGLONG now = GetTickCount64();

    int trayLeft = width;
    std::vector<OccupiedSpan> occupied;
    bool knownLayout = false;
    if (snapshot.CanReuse(width, height, now)) {
        occupied = snapshot.spans;
        trayLeft = snapshot.trayLeft;
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
    HWND mainWnd = g_hMainWnd.load();
    if (mainWnd && (hwnd == mainWnd || IsChild(mainWnd, hwnd))) return false;
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
    StartScanThread();
    if (g_hTaskbar) {
        DWORD explorerPid = 0;
        GetWindowThreadProcessId(g_hTaskbar, &explorerPid);
        if (explorerPid && (explorerPid != g_hookPid || g_hTaskbar != g_hookTaskbar)) {
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
        }
    }
    if (!g_hHookCreate)
        g_hHookCreate = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE,
            NULL, CreateWinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    RequestScan();
}

void CleanupTaskbarHooks() {
    g_hMainWnd = nullptr;
    g_placementTimerMs = 0;
    {
        AcquireSRWLockExclusive(&g_layoutLock);
        g_layout = {};
        ReleaseSRWLockExclusive(&g_layoutLock);
    }
    if (g_hHookLoc) { UnhookWinEvent(g_hHookLoc); g_hHookLoc = NULL; }
    if (g_hHookShow) { UnhookWinEvent(g_hHookShow); g_hHookShow = NULL; }
    if (g_hHookHide) { UnhookWinEvent(g_hHookHide); g_hHookHide = NULL; }
    if (g_hHookCreate) { UnhookWinEvent(g_hHookCreate); g_hHookCreate = NULL; }
    g_hookPid = 0;
    g_hookTaskbar = NULL;
    g_syncPending = false;
}

void ShutdownTaskbarHooks() {
    g_scanStop = true;
    if (g_scanEvent) SetEvent(g_scanEvent);
    if (g_scanThread) {
        WaitForSingleObject(g_scanThread, 2000);
        CloseHandle(g_scanThread);
        g_scanThread = NULL;
    }
    if (g_scanEvent) {
        CloseHandle(g_scanEvent);
        g_scanEvent = NULL;
    }
}
