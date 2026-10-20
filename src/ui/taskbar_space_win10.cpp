#include "ui/taskbar_space_win10.h"

#include <algorithm>

namespace nsm {
namespace {

bool IsHorizontal(Edge e) { return e == Edge::Bottom || e == Edge::Top; }

}  // namespace

Rect ReservedTaskListRect(const Rect& natural, const Rect& overlay, Edge edge, int margin) {
    if (overlay.Empty()) return natural;
    Rect r = natural;
    if (IsHorizontal(edge)) {
        r.right = std::clamp(overlay.left - margin, natural.left, natural.right);
    } else {
        r.bottom = std::clamp(overlay.top - margin, natural.top, natural.bottom);
    }
    return r;
}

bool ReapplyLimiter::Allow(uint64_t nowMs) {
    if (nowMs < blockedUntil_) return false;
    while (!recent_.empty() && recent_.front() + windowMs_ <= nowMs) recent_.pop_front();
    if (static_cast<int>(recent_.size()) >= maxApplies_) {
        blockedUntil_ = nowMs + backoffMs_;
        recent_.clear();
        return false;
    }
    recent_.push_back(nowMs);
    return true;
}

void TaskListReservation::Attach(const Rect& current) {
    attached_ = true;
    dirty_ = false;
    natural_ = applied_ = current;
    overlay_ = {};
    edge_ = Edge::Bottom;
    margin_ = 0;
}

void TaskListReservation::Detach() { attached_ = false; }

std::optional<Rect> TaskListReservation::SetOverlay(const Rect& overlay, Edge edge, int margin, const Rect& current,
                                                    uint64_t nowMs) {
    if (!attached_ || (overlay == overlay_ && edge == edge_ && margin == margin_)) return std::nullopt;
    overlay_ = overlay;
    edge_ = edge;
    margin_ = margin;
    return Step(current, nowMs);
}

std::optional<Rect> TaskListReservation::Observe(const Rect& current, uint64_t nowMs) {
    if (!attached_) return std::nullopt;
    if (current != applied_) {
        natural_ = current;  // Explorer laid the task list out again
    } else if (!dirty_) {
        return std::nullopt;  // as we left it
    }
    return Step(current, nowMs);
}

std::optional<Rect> TaskListReservation::RestoreTarget(const Rect& current) const {
    if (!attached_ || current != applied_ || current == natural_) return std::nullopt;
    return natural_;
}

void TaskListReservation::Unapplied(const Rect& current) {
    if (!attached_) return;
    applied_ = current;  // so Observe doesn't take it for a new layout of Explorer's
    dirty_ = true;
}

std::optional<Rect> TaskListReservation::Step(const Rect& current, uint64_t nowMs) {
    const Rect target = ReservedTaskListRect(natural_, overlay_, edge_, margin_);
    if (target == current) {
        applied_ = current;
        dirty_ = false;
        return std::nullopt;
    }
    if (!limiter_.Allow(nowMs)) {
        dirty_ = true;  // owed; a later Observe retries
        return std::nullopt;
    }
    applied_ = target;
    dirty_ = false;
    return target;
}

bool TaskbarSpaceWin10::Attach(HWND taskbar) {
    Restore();
    HWND rebar = FindWindowExW(taskbar, nullptr, L"ReBarWindow32", nullptr);
    HWND taskSw = rebar ? FindWindowExW(rebar, nullptr, L"MSTaskSwWClass", nullptr) : nullptr;
    if (!taskSw) return false;
    rebar_ = rebar;
    taskSw_ = taskSw;
    state_.Attach(CurrentInRebar());
    return true;
}

void TaskbarSpaceWin10::Reserve(const Rect& overlay, Edge edge, int margin) {
    if (!taskSw_ || writing_) return;
    // Keep the overlay in rebar client coordinates: the taskbar can slide (auto-hide) without
    // changing where the task list must end.
    Rect inRebar;
    if (!overlay.Empty()) {
        RECT r{overlay.left, overlay.top, overlay.right, overlay.bottom};
        MapWindowPoints(HWND_DESKTOP, rebar_, reinterpret_cast<POINT*>(&r), 2);
        inRebar = {r.left, r.top, r.right, r.bottom};
    }
    if (auto target = state_.SetOverlay(inRebar, edge, margin, CurrentInRebar(), GetTickCount64())) Write(*target);
}

bool TaskbarSpaceWin10::OnLocationChange(HWND hwnd) {
    if (!taskSw_ || writing_ || (hwnd != taskSw_ && hwnd != rebar_)) return false;
    if (!IsWindow(taskSw_)) {  // Explorer went away; nothing left to restore
        state_.Detach();
        rebar_ = taskSw_ = nullptr;
        return false;
    }
    auto target = state_.Observe(CurrentInRebar(), GetTickCount64());
    return target && Write(*target);
}

void TaskbarSpaceWin10::Restore() {
    if (writing_) {  // re-entered from inside Write's SetWindowPos: finish that first
        restoreAfterWrite_ = true;
        return;
    }
    if (taskSw_ && IsWindow(taskSw_)) {
        if (auto target = state_.RestoreTarget(CurrentInRebar())) Write(*target, true);
    }
    state_.Detach();
    rebar_ = taskSw_ = nullptr;
}

bool TaskbarSpaceWin10::Write(const Rect& r, bool restore) {
    // A hung Explorer would block a synchronous SetWindowPos, and with it our UI thread (and
    // WM_ENDSESSION). Skip a resize, which a later check retries; post the restore, which then
    // lands when Explorer recovers, after anything we wrote before.
    const bool hung = IsHungAppWindow(GetAncestor(taskSw_, GA_ROOT)) != FALSE;
    if (hung && !restore) {
        state_.Unapplied(CurrentInRebar());
        return false;
    }
    // Otherwise synchronous on purpose: the location-change events that follow must see the
    // final rect, or an in-flight resize could be mistaken for Explorer's natural layout. It
    // pumps sent messages while Explorer handles it, hence the writing_ guard.
    writing_ = true;
    SetWindowPos(taskSw_, nullptr, r.left, r.top, r.Width(), r.Height(),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | (hung ? SWP_ASYNCWINDOWPOS : 0));
    writing_ = false;
    if (restoreAfterWrite_) {
        restoreAfterWrite_ = false;
        Restore();
    }
    return true;
}

Rect TaskbarSpaceWin10::CurrentInRebar() const {
    RECT r{};
    GetWindowRect(taskSw_, &r);
    MapWindowPoints(HWND_DESKTOP, rebar_, reinterpret_cast<POINT*>(&r), 2);
    return {r.left, r.top, r.right, r.bottom};
}

}  // namespace nsm
