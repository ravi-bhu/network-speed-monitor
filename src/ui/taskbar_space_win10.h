#pragma once
#include <windows.h>

#include "core/overlay_layout.h"

namespace nsm {

// Windows 10 only: keeps the task buttons out of the overlay's way by shrinking Explorer's
// task-switch window (MSTaskSwWClass, a child of ReBarWindow32 in Shell_TrayWnd) from its
// right or bottom end. The task list then scrolls instead of running under the overlay.
// Explorer resizes the window back whenever it re-lays out the rebar; call OnLocationChange
// for rebar/task-switch location changes so the reservation is re-applied. Restore() must run
// before exit (Shutdown, WM_ENDSESSION): a killed process leaves the task list shrunk until
// Explorer next lays out the taskbar.
class TaskbarSpaceWin10 {
public:
    TaskbarSpaceWin10() = default;
    ~TaskbarSpaceWin10() { Restore(); }
    TaskbarSpaceWin10(const TaskbarSpaceWin10&) = delete;
    TaskbarSpaceWin10& operator=(const TaskbarSpaceWin10&) = delete;

    // Finds the rebar and task-switch windows of `taskbar` (Shell_TrayWnd) and records the
    // task-switch window's current rect as its natural size. False if they aren't there.
    bool Attach(HWND taskbar);
    // Shrinks the task list so it ends `margin` before `overlay` (screen px); an empty overlay
    // restores the natural size. Remembers the request for re-application.
    void Reserve(const Rect& overlay, Edge edge, int margin);
    // Call for every location change of Rebar() or TaskSwitch(): if Explorer resized the task
    // list, adopt that as the new natural size and re-apply. Returns true if it re-applied.
    bool OnLocationChange(HWND hwnd);
    // Puts the task list back to its natural size and detaches.
    void Restore();

    bool Attached() const { return taskSw_ != nullptr; }
    HWND Rebar() const { return rebar_; }
    HWND TaskSwitch() const { return taskSw_; }

private:
    void Apply();
    RECT CurrentInRebar() const;  // task-switch rect in rebar client coordinates

    HWND rebar_ = nullptr;
    HWND taskSw_ = nullptr;
    RECT natural_{};  // rebar client coordinates
    RECT applied_{};  // what we last set, rebar client coordinates
    Rect overlay_;
    Edge edge_ = Edge::Bottom;
    int margin_ = 0;
};

// Pure: the task-list rect (screen px) that ends `margin` before the overlay, never larger
// than `natural` and never inverted. An empty overlay yields `natural`.
Rect ReservedTaskListRect(const Rect& natural, const Rect& overlay, Edge edge, int margin);

}  // namespace nsm
