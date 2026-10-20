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
    void Reset() {
        blockedUntil_ = 0;
        recent_.clear();
    }

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

    // The current rect is the natural size, and the limiter starts afresh. A natural rect that
    // ends exactly where the reservation would end it was left shrunk by an instance that was
    // killed before it restored it: its far end is then taken as the overlay's far end plus the
    // margin (the tray, where PlaceOverlay ends the free space).
    void Attach(const Rect& current);
    void Detach();
    bool Attached() const { return attached_; }
    // The task list has the rect the reservation wants (as far as the last check knows), so it
    // can't reach the overlay. False while a write is Owed.
    bool InEffect() const { return attached_ && !dirty_; }
    // A wanted rect was refused by the limiter and is still owed: the task list may be at its
    // natural size, under the overlay.
    bool Owed() const { return attached_ && dirty_; }
    // The overlay (empty: none) moved or changed size.
    std::optional<Rect> SetOverlay(const Rect& overlay, Edge edge, int margin, const Rect& current, uint64_t nowMs);
    // A location change, or a periodic check. A rect other than the one we set means Explorer
    // laid the task list out again: adopt it as the natural size and re-apply (rate-limited).
    // If it is our shrunk rect moved (same far end, or same length), the natural far end is
    // kept, so a band moving can't turn the shrunk width into the natural one.
    std::optional<Rect> Observe(const Rect& current, uint64_t nowMs);
    // What to write back on exit: the natural size, but only while the task list still has the
    // rect we set. Once Explorer has changed it, the layout is Explorer's and is left alone.
    std::optional<Rect> RestoreTarget(const Rect& current) const;

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
// list shrunk until Explorer next lays out the taskbar, or until the next instance recognises
// the shrink and restores it on its own exit.
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
    // asks for the natural size. Called while a write is in flight, it runs once that returns.
    void Reserve(const Rect& overlay, Edge edge, int margin);
    // Call for location changes of Rebar() or TaskSwitch(), and periodically: re-applies if
    // Explorer re-laid out the task list (rate-limited). Returns true if it wrote a new rect.
    bool OnLocationChange(HWND hwnd);
    // Puts the task list back to its natural size, unless Explorer has re-laid it out since, and
    // detaches. Called while a write to Explorer is in flight (a re-entrant WM_ENDSESSION), it
    // runs as soon as that write returns, so the shrink can't land after the restore.
    void Restore();

    bool Attached() const { return taskSw_ != nullptr; }
    // The task list is known to end before the overlay. False while the limiter backs off or a
    // reservation is waiting for a write: the task buttons can then reach the overlay.
    bool InEffect() const { return taskSw_ != nullptr && !pending_ && state_.InEffect(); }
    HWND Rebar() const { return rebar_; }
    HWND TaskSwitch() const { return taskSw_; }

private:
    void Write(const Rect& r);
    Rect CurrentInRebar() const;  // task-switch rect in rebar client coordinates

    struct Request {
        Rect overlay;
        Edge edge = Edge::Bottom;
        int margin = 0;
    };

    HWND rebar_ = nullptr;
    HWND taskSw_ = nullptr;
    TaskListReservation state_;
    bool writing_ = false;
    bool restoreAfterWrite_ = false;
    std::optional<Request> pending_;  // a Reserve that arrived during a write
};

}  // namespace nsm
