#pragma once
#include <windows.h>

#include <cstdint>
#include <deque>
#include <optional>

#include "core/overlay_layout.h"

namespace nsm {

// Pure: the task-list rect that ends `margin` before the overlay (any one coordinate space),
// never larger than `natural` and never inverted. An empty overlay yields `natural`.
Rect ReservedTaskListRect(const Rect& natural, const Rect& overlay, Edge edge, int margin);

// Pure: damps re-applying the reservation. More than `maxApplies` within `windowMs` means we
// are fighting Explorer's layout: refuse for `backoffMs`, then start counting afresh.
class ReapplyLimiter {
public:
    explicit ReapplyLimiter(int maxApplies = 5, uint64_t windowMs = 2000, uint64_t backoffMs = 10000)
        : maxApplies_(maxApplies), windowMs_(windowMs), backoffMs_(backoffMs) {}
    bool Allow(uint64_t nowMs);  // true: go ahead (and counted)

private:
    int maxApplies_;
    uint64_t windowMs_, backoffMs_;
    uint64_t blockedUntil_ = 0;
    std::deque<uint64_t> recent_;
};

// Pure state machine behind TaskbarSpaceWin10, in rebar client coordinates. Each call gets the
// task list's current rect and returns the rect to write, if any (it assumes the write lands).
class TaskListReservation {
public:
    explicit TaskListReservation(ReapplyLimiter limiter = ReapplyLimiter()) : limiter_(limiter) {}

    void Attach(const Rect& current);  // the current rect is the natural size
    void Detach();
    bool Attached() const { return attached_; }
    // The overlay (empty: none) moved or changed size.
    std::optional<Rect> SetOverlay(const Rect& overlay, Edge edge, int margin, const Rect& current, uint64_t nowMs);
    // A location change, or a periodic check. A rect other than the one we set means Explorer
    // laid the task list out again: adopt it as the natural size and re-apply (rate-limited).
    std::optional<Rect> Observe(const Rect& current, uint64_t nowMs);
    // What to write back on exit: the natural size, but only while the task list still has the
    // rect we set. Once Explorer has changed it, the layout is Explorer's and is left alone.
    std::optional<Rect> RestoreTarget(const Rect& current) const;
    // The rect just returned was not written (Explorer was hung): the task list still has
    // `current`. The wanted rect stays owed, and a later Observe retries it.
    void Unapplied(const Rect& current);

private:
    std::optional<Rect> Step(const Rect& current, uint64_t nowMs);

    ReapplyLimiter limiter_;
    bool attached_ = false;
    bool dirty_ = false;  // a wanted rect was refused by the limiter and is still owed
    Rect natural_, applied_, overlay_;
    Edge edge_ = Edge::Bottom;
    int margin_ = 0;
};

// Windows 10 only: keeps the task buttons out of the overlay's way by shrinking Explorer's
// task-switch window (MSTaskSwWClass, a child of ReBarWindow32 in Shell_TrayWnd) from its
// right or bottom end. The task list then scrolls instead of running under the overlay.
// Explorer resizes the window back whenever it re-lays out the rebar; call OnLocationChange
// for rebar/task-switch location changes (and periodically) so the reservation is re-applied.
// Restore() must run before exit (Shutdown, WM_ENDSESSION): a killed process leaves the task
// list shrunk until Explorer next lays out the taskbar.
class TaskbarSpaceWin10 {
public:
    TaskbarSpaceWin10() = default;
    ~TaskbarSpaceWin10() { Restore(); }
    TaskbarSpaceWin10(const TaskbarSpaceWin10&) = delete;
    TaskbarSpaceWin10& operator=(const TaskbarSpaceWin10&) = delete;

    // Finds the rebar and task-switch windows of `taskbar` (Shell_TrayWnd) and records the
    // task-switch window's current rect as its natural size. False if they aren't there.
    bool Attach(HWND taskbar);
    // Shrinks the task list so it ends `margin` px before `overlay`; an empty overlay asks for
    // the natural size. `overlay` is in screen px; it is mapped to rebar client coordinates,
    // where the task list is measured and kept, so the auto-hide slide doesn't move it.
    void Reserve(const Rect& overlay, Edge edge, int margin);
    // Call for location changes of Rebar() or TaskSwitch(), and periodically: re-applies if
    // Explorer re-laid out the task list (rate-limited). Returns true if it wrote a new rect.
    bool OnLocationChange(HWND hwnd);
    // Puts the task list back to its natural size, unless Explorer has re-laid it out since, and
    // detaches. Called while a write to Explorer is in flight (a re-entrant WM_ENDSESSION), it
    // runs as soon as that write returns, so the shrink can't land after the restore. A hung
    // Explorer gets the restore posted (SWP_ASYNCWINDOWPOS) instead, so exit never blocks on it.
    void Restore();

    bool Attached() const { return taskSw_ != nullptr; }
    HWND Rebar() const { return rebar_; }
    HWND TaskSwitch() const { return taskSw_; }

private:
    // Writes `r` (rebar client coordinates) to the task list. While Explorer is hung a resize is
    // skipped and stays owed; a restore is posted instead. False if nothing was written.
    bool Write(const Rect& r, bool restore = false);
    Rect CurrentInRebar() const;  // task-switch rect in rebar client coordinates

    HWND rebar_ = nullptr;
    HWND taskSw_ = nullptr;
    TaskListReservation state_;
    bool writing_ = false;
    bool restoreAfterWrite_ = false;
};

}  // namespace nsm
