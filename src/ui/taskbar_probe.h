#pragma once
#include <windows.h>

#include <vector>

#include "core/overlay_layout.h"

namespace nsm {

struct TaskbarInfo {
    HWND hwnd;
    HMONITOR monitor;
    bool primary;
    bool trayFound;  // false: geom.tray is FallbackTray (primary: no TrayNotifyWnd; secondary: no clock)
    TaskbarGeometry geom;
};

// Which taskbars get their `occupied` extent measured (a cross-process UI Automation query,
// the expensive part of a probe). Skipped taskbars report `occupied` empty.
enum class OccupiedProbe { All, SecondaryOnly, None };

// Shell_TrayWnd (primary, first) + every Shell_SecondaryTrayWnd. Physical pixels; the caller
// must be per-monitor DPI aware v2. The occupied extent uses UI Automation and needs COM
// initialized (STA) on the calling thread; without it `occupied` is left empty. On Windows 10
// the query starts at the taskbar's MSTaskListWClass window (task buttons only).
std::vector<TaskbarInfo> ProbeTaskbars(OccupiedProbe occupied = OccupiedProbe::All);

bool IsWindows11();  // build >= 22000 via RtlGetVersion

// Pure helpers behind ProbeTaskbars (exposed for tests).
Edge EdgeFromRects(const Rect& taskbar, const Rect& monitor);        // nearest monitor edge
Rect FallbackTray(const Rect& taskbar, Edge edge);                    // zero-size rect at the far end
Rect OccupiedExtent(const std::vector<Rect>& buttons, const Rect& taskbar, const Rect& tray, Edge edge);

}  // namespace nsm
