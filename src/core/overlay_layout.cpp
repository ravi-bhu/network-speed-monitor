#include "core/overlay_layout.h"

#include <algorithm>

namespace nsm {

namespace {

// Same rounding as Win32 MulDiv: round half away from zero.
int MulDiv(int number, int numerator, int denominator) {
    long long prod = static_cast<long long>(number) * numerator;
    long long half = denominator / 2;
    long long r = (prod >= 0) ? (prod + half) / denominator : (prod - half) / denominator;
    return static_cast<int>(r);
}

bool IsHorizontal(Edge e) { return e == Edge::Bottom || e == Edge::Top; }

}  // namespace

OverlayPlacement PlaceOverlay(const TaskbarGeometry& g, const OverlaySizes& s, bool wantGraph) {
    OverlayPlacement p;
    const int margin = MulDiv(4, static_cast<int>(g.dpi), 96);
    // An empty tray (none found) ends the free space at the taskbar's far end, as FallbackTray does.
    const bool noTray = g.tray.Empty();

    if (IsHorizontal(g.edge)) {
        const int right = (noTray ? g.taskbar.right : g.tray.left) - margin;
        const int start = g.occupied.Empty() ? g.taskbar.left : g.occupied.right + margin;
        const int freeSpace = right - start;
        int width;
        if (wantGraph && s.fullWidth <= freeSpace) {
            width = s.fullWidth;
            p.showGraph = true;
        } else if (s.textOnlyWidth <= freeSpace) {
            width = s.textOnlyWidth;
        } else if (s.compactWidth > 0 && s.compactWidth <= freeSpace) {
            width = s.compactWidth;
            p.compact = true;
        } else {
            return p;
        }
        // Small taskbar buttons make the bar thinner than the readout: clamp to the bar, don't overhang.
        const int height = std::min(s.height, g.taskbar.Height());
        const int top = g.taskbar.top + (g.taskbar.Height() - height) / 2;
        p.visible = true;
        p.rect = {right - width, top, right, top + height};
        return p;
    }

    const int width = g.taskbar.Width() - 2 * margin;
    // A default-width left/right taskbar (about 70 DIP) is narrower than the normal text.
    const bool compact = width < s.textOnlyWidth;
    if (compact && (s.compactWidth <= 0 || width < s.compactWidth)) return p;
    const bool graph = !compact && wantGraph && width >= s.fullWidth;
    const int height = graph ? 2 * s.height : s.height;
    const int bottom = (noTray ? g.taskbar.bottom : g.tray.top) - margin;
    const int top = bottom - height;
    const int limit = g.occupied.Empty() ? g.taskbar.top : g.occupied.bottom + margin;
    if (top < limit) return p;
    p.visible = true;
    p.showGraph = graph;
    p.compact = compact;
    p.rect = {g.taskbar.left + margin, top, g.taskbar.right - margin, bottom};
    return p;
}

bool IsTaskbarShown(const Rect& taskbar, const Rect& monitor, Edge edge) {
    int thickness, visible;
    if (IsHorizontal(edge)) {
        thickness = taskbar.Height();
        visible = std::min(taskbar.bottom, monitor.bottom) - std::max(taskbar.top, monitor.top);
    } else {
        thickness = taskbar.Width();
        visible = std::min(taskbar.right, monitor.right) - std::max(taskbar.left, monitor.left);
    }
    if (thickness <= 0) return false;
    return visible * 2 >= thickness;
}

bool IsFullscreenOnMonitor(const Rect& window, const Rect& monitor, bool windowIsShell, bool maximizedWindow) {
    if (windowIsShell || maximizedWindow) return false;
    return window.left <= monitor.left && window.top <= monitor.top &&
           window.right >= monitor.right && window.bottom >= monitor.bottom;
}

}  // namespace nsm
