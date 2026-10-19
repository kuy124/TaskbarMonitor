#include "TaskbarPlacement.h"
#include <cassert>

int main() {
    TaskbarLayoutSnapshot snapshot;
    assert(!snapshot.CanReuse(1920, 60, 100));
    snapshot = {{{0, 100}, {700, 1920}}, 1920, 60, 1600, 100, true};
    assert(snapshot.CanReuse(1920, 60, 200)); // transient UIA failure
    assert(snapshot.CanReuse(1920, 60, 2100));
    assert(!snapshot.CanReuse(1920, 60, 2101)); // bounded stale data
    assert(!snapshot.CanReuse(1280, 60, 200)); // display changed
    assert(!snapshot.CanReuse(1920, 48, 200));
    assert(!snapshot.CanReuse(1920, 60, 99));
    assert(FindFreeTaskbarPosition(snapshot.width, 449, 12, snapshot.spans) == 108);
    // Captured on a 1920 px taskbar: the 449 px monitor fits left of
    // Search and app buttons. The full-width XAML host is a background.
    assert(IsTaskbarBackgroundClass(L"Windows.UI.Composition.DesktopWindowContentBridge"));
    assert(!IsTaskbarBackgroundClass(L"TrayNotifyWnd"));
    std::vector<OccupiedSpan> captured = {
        {683, 1128},  // native ReBar
        {1629, 1920}, // native notification area
        {627, 683},   // UI Automation Start
        {686, 961},   // UI Automation Search
        {963, 1293},  // UI Automation app buttons beyond ReBar
    };
    assert(FindFreeTaskbarPosition(1920, 449, 12, captured) == 12);
    captured.push_back({0, 100}); // widgets take part of the left gap
    assert(FindFreeTaskbarPosition(1920, 449, 12, captured) == 108);

    // Search grows into the preferred left-side position.
    assert(FindFreeTaskbarPosition(1000, 120, 20,
        {{0, 100}, {400, 700}, {850, 1000}}) == 108);
    assert(FindFreeTaskbarPosition(1000, 120, 20,
        {{0, 250}, {400, 700}, {850, 1000}}) == 258);

    // Centered app buttons leave equally close gaps on either side.
    assert(FindFreeTaskbarPosition(1200, 240, 480,
        {{420, 780}, {1050, 1200}}) == 172);

    // A growing notification area pushes the monitor to the remaining gap.
    assert(FindFreeTaskbarPosition(1200, 180, 980,
        {{400, 700}, {1050, 1200}}) == 862);
    assert(FindFreeTaskbarPosition(1200, 180, 980,
        {{400, 700}, {900, 1200}}) == 712);

    // Custom coordinates are a preference, including when occupied.
    assert(FindFreeTaskbarPosition(800, 100, 250, {{200, 400}}) == 92);

    // A full monitor has no in-taskbar position until a gap reopens.
    assert(!FindFreeTaskbarPosition(500, 200, 130, {{0, 160}, {320, 500}}));
    assert(FindFreeTaskbarPosition(500, 200, 130,
        {{0, 90}, {320, 500}}) == 112);
    assert(FindFreeTaskbarPosition(500, 200, 130, {{0, 100}, {316, 500}}) == 108);

    const PlacementRect screen = {0, 0, 1920, 1080};
    const PlacementRect bottom = {0, 1020, 1920, 1080};
    assert(DetectTaskbarEdge(bottom, screen) == TaskbarEdge::Bottom);
    assert(IsTaskbarFullyOpen(bottom, screen, TaskbarEdge::Bottom));
    assert(!IsTaskbarFullyOpen({0, 1078, 1920, 1138}, screen, TaskbarEdge::Bottom));
    assert(!IsTaskbarFullyOpen({0, 1040, 1920, 1100}, screen, TaskbarEdge::Bottom));

    const PlacementRect top = {0, 0, 1920, 60};
    assert(DetectTaskbarEdge(top, screen) == TaskbarEdge::Top);
    assert(IsTaskbarFullyOpen(top, screen, TaskbarEdge::Top));

    const PlacementRect left = {0, 0, 60, 1080};
    const PlacementRect right = {1860, 0, 1920, 1080};
    assert(DetectTaskbarEdge(left, screen) == TaskbarEdge::Left);
    assert(DetectTaskbarEdge(right, screen) == TaskbarEdge::Right);
}
