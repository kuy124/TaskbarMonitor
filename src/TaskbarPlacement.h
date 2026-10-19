#pragma once

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <optional>
#include <vector>

struct OccupiedSpan {
    int left;
    int right;
};

struct PlacementRect {
    int left, top, right, bottom;
};

enum class TaskbarEdge { Bottom, Top, Left, Right };

// Only reuse a snapshot while its taskbar geometry still matches. A failed
// accessibility read is not evidence that the last measured gap disappeared.
struct TaskbarLayoutSnapshot {
    std::vector<OccupiedSpan> spans;
    int width = 0, height = 0, trayLeft = 0;
    unsigned long long measuredAt = 0;
    bool valid = false;

    bool CanReuse(int w, int h, unsigned long long now) const {
        return valid && width == w && height == h &&
               now >= measuredAt && now - measuredAt <= 2000;
    }
};

inline bool IsTaskbarBackgroundClass(const wchar_t* cls) {
    return std::wcscmp(cls, L"Windows.UI.Composition.DesktopWindowContentBridge") == 0 ||
           std::wcscmp(cls, L"Windows.UI.Core.CoreWindow") == 0 ||
           std::wcscmp(cls, L"Windows.UI.Input.InputSite.WindowClass") == 0;
}

inline TaskbarEdge DetectTaskbarEdge(const PlacementRect& taskbar, const PlacementRect& monitor) {
    if (taskbar.right - taskbar.left >= taskbar.bottom - taskbar.top) {
        return taskbar.top + taskbar.bottom >= monitor.top + monitor.bottom
            ? TaskbarEdge::Bottom : TaskbarEdge::Top;
    }
    return taskbar.left + taskbar.right >= monitor.left + monitor.right
        ? TaskbarEdge::Right : TaskbarEdge::Left;
}

inline bool IsTaskbarFullyOpen(const PlacementRect& taskbar,
                               const PlacementRect& monitor, TaskbarEdge edge) {
    if (taskbar.left >= taskbar.right || taskbar.top >= taskbar.bottom) return false;
    switch (edge) {
    case TaskbarEdge::Bottom: return std::abs(taskbar.bottom - monitor.bottom) <= 2;
    case TaskbarEdge::Top: return std::abs(taskbar.top - monitor.top) <= 2;
    case TaskbarEdge::Left: return std::abs(taskbar.left - monitor.left) <= 2;
    case TaskbarEdge::Right: return std::abs(taskbar.right - monitor.right) <= 2;
    }
    return false;
}

// Finds the closest full-width gap to the preferred taskbar coordinate.
inline std::optional<int> FindFreeTaskbarPosition(
    int taskbarWidth, int windowWidth, int preferredX,
    std::vector<OccupiedSpan> occupied, int clearance = 8) {
    if (taskbarWidth < windowWidth || windowWidth <= 0) return std::nullopt;

    for (auto& span : occupied) {
        span.left = std::max(0, span.left - clearance);
        span.right = std::min(taskbarWidth, span.right + clearance);
    }
    occupied.erase(std::remove_if(occupied.begin(), occupied.end(),
        [](const OccupiedSpan& span) { return span.left >= span.right; }), occupied.end());
    std::sort(occupied.begin(), occupied.end(),
        [](const OccupiedSpan& a, const OccupiedSpan& b) { return a.left < b.left; });

    std::optional<int> best;
    int cursor = 0;
    auto tryGap = [&](int left, int right) {
        if (right - left < windowWidth) return;
        int candidate = std::clamp(preferredX, left, right - windowWidth);
        if (!best || std::abs(candidate - preferredX) < std::abs(*best - preferredX)) {
            best = candidate;
        }
    };
    for (const auto& span : occupied) {
        tryGap(cursor, span.left);
        cursor = std::max(cursor, span.right);
    }
    tryGap(cursor, taskbarWidth);
    return best;
}
