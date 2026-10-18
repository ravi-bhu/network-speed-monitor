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

struct OverlaySizes { int fullWidth, textOnlyWidth, height; };  // physical px at geometry.dpi

struct OverlayPlacement {
    bool visible = false;
    bool showGraph = false;
    Rect rect;
};

OverlayPlacement PlaceOverlay(const TaskbarGeometry& g, const OverlaySizes& s, bool wantGraph);
bool IsTaskbarShown(const Rect& taskbar, const Rect& monitor, Edge edge);
bool IsFullscreenOnMonitor(const Rect& window, const Rect& monitor, bool windowIsShell);

}  // namespace nsm
