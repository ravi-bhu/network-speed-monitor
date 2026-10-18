#include "ui/taskbar_space_win10.h"

#include <algorithm>

namespace nsm {
namespace {

bool IsHorizontal(Edge e) { return e == Edge::Bottom || e == Edge::Top; }

bool SameRect(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

Rect ToRect(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }
RECT ToRECT(const Rect& r) { return {r.left, r.top, r.right, r.bottom}; }

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

bool TaskbarSpaceWin10::Attach(HWND taskbar) {
    Restore();
    HWND rebar = FindWindowExW(taskbar, nullptr, L"ReBarWindow32", nullptr);
    HWND taskSw = rebar ? FindWindowExW(rebar, nullptr, L"MSTaskSwWClass", nullptr) : nullptr;
    if (!taskSw) return false;
    rebar_ = rebar;
    taskSw_ = taskSw;
    natural_ = applied_ = CurrentInRebar();
    overlay_ = {};
    return true;
}

void TaskbarSpaceWin10::Reserve(const Rect& overlay, Edge edge, int margin) {
    if (!taskSw_) return;
    // Keep the overlay in rebar client coordinates: the taskbar can slide (auto-hide) without
    // changing where the task list must end.
    RECT r = ToRECT(overlay);
    if (!overlay.Empty()) MapWindowPoints(HWND_DESKTOP, rebar_, reinterpret_cast<POINT*>(&r), 2);
    const Rect inRebar = overlay.Empty() ? Rect{} : ToRect(r);
    if (inRebar == overlay_ && edge == edge_ && margin == margin_) return;
    overlay_ = inRebar;
    edge_ = edge;
    margin_ = margin;
    Apply();
}

bool TaskbarSpaceWin10::OnLocationChange(HWND hwnd) {
    if (!taskSw_ || (hwnd != taskSw_ && hwnd != rebar_)) return false;
    if (!IsWindow(taskSw_)) {  // Explorer went away; nothing left to restore
        rebar_ = taskSw_ = nullptr;
        return false;
    }
    const RECT current = CurrentInRebar();
    if (SameRect(current, applied_)) return false;
    natural_ = current;  // Explorer laid the task list out again
    Apply();
    return true;
}

void TaskbarSpaceWin10::Restore() {
    if (taskSw_ && IsWindow(taskSw_) && !SameRect(CurrentInRebar(), natural_)) {
        SetWindowPos(taskSw_, nullptr, natural_.left, natural_.top, natural_.right - natural_.left,
                     natural_.bottom - natural_.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    rebar_ = taskSw_ = nullptr;
    natural_ = applied_ = {};
    overlay_ = {};
}

void TaskbarSpaceWin10::Apply() {
    const RECT target = ToRECT(ReservedTaskListRect(ToRect(natural_), overlay_, edge_, margin_));
    applied_ = target;
    if (SameRect(CurrentInRebar(), target)) return;
    // Synchronous on purpose: the location-change events that follow must see the final rect,
    // or an in-flight resize could be mistaken for Explorer's natural layout.
    SetWindowPos(taskSw_, nullptr, target.left, target.top, target.right - target.left, target.bottom - target.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

RECT TaskbarSpaceWin10::CurrentInRebar() const {
    RECT r{};
    GetWindowRect(taskSw_, &r);
    MapWindowPoints(HWND_DESKTOP, rebar_, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

}  // namespace nsm
