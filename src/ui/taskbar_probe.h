#pragma once
#include <windows.h>

#include <vector>

#include "core/overlay_layout.h"

namespace nsm {

struct TaskbarInfo {
    HWND hwnd;
    HMONITOR monitor;
    bool primary;
    TaskbarGeometry geom;
};

// Shell_TrayWnd (primary, first) + every Shell_SecondaryTrayWnd. Physical pixels; the caller
// must be per-monitor DPI aware v2. The occupied extent uses UI Automation and needs COM
// initialized (STA) on the calling thread; without it `occupied` is left empty.
std::vector<TaskbarInfo> ProbeTaskbars();

bool IsWindows11();  // build >= 22000 via RtlGetVersion

// Pure helpers behind ProbeTaskbars (exposed for tests).
Edge EdgeFromRects(const Rect& taskbar, const Rect& monitor);        // nearest monitor edge
Rect FallbackTray(const Rect& taskbar, Edge edge);                    // zero-size rect at the far end
Rect OccupiedExtent(const std::vector<Rect>& buttons, const Rect& taskbar, const Rect& tray, Edge edge);

}  // namespace nsm
