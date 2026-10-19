#pragma once

namespace nsm {

enum class Edge { Bottom, Top, Left, Right };

struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
    bool Empty() const { return Width() <= 0 || Height() <= 0; }
    bool operator==(const Rect&) const = default;
};

struct TaskbarGeometry {
    Rect monitor, taskbar, tray, occupied;  // occupied: other taskbar content (may be empty)
    Edge edge;
    unsigned dpi;
};

// Physical px at geometry.dpi. compactWidth is the text in its compact form ("↑ 15.8MB"), the
// last resort before hiding on a narrow (default-width vertical) or crowded taskbar; 0 = none.
struct OverlaySizes {
    int fullWidth, textOnlyWidth, height;
    int compactWidth = 0;
};

struct OverlayPlacement {
    bool visible = false;
    bool showGraph = false;
    bool compact = false;  // draw the values with FormatSpeedCompact
    Rect rect;
};

OverlayPlacement PlaceOverlay(const TaskbarGeometry& g, const OverlaySizes& s, bool wantGraph);
bool IsTaskbarShown(const Rect& taskbar, const Rect& monitor, Edge edge);
// maximizedWindow: a maximized window with a title bar. With an auto-hidden taskbar it covers
// the whole monitor, but it is an ordinary app, not a fullscreen game or video.
bool IsFullscreenOnMonitor(const Rect& window, const Rect& monitor, bool windowIsShell, bool maximizedWindow = false);

}  // namespace nsm
