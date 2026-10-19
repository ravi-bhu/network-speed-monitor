#include "ui/overlay_manager.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>

#include "platform/strings.h"
#include "platform/theme.h"
#include "resource.h"
#include "ui/overlay_renderer.h"
#include "ui/overlay_window.h"

namespace nsm {

struct OverlayManager::Bar {
    TaskbarInfo info{};
    OverlayWindow window;
    HWND tooltip = nullptr;  // owned by `window`, destroyed with it
    SIZE toolSize{};
    std::optional<OverlaySizes> sizes;
    unsigned sizesDpi = 0;
    bool sizesLetters = false;
    OverlayPlacement placement;
};

namespace {

constexpr std::array<std::wstring_view, 4> kShellClasses{L"Progman", L"WorkerW", L"Shell_TrayWnd",
                                                         L"Shell_SecondaryTrayWnd"};
constexpr std::array<std::wstring_view, 5> kFlyoutClasses{
    L"Windows.UI.Core.CoreWindow",     // Win10 Start, search, Action Center, tray flyouts
    L"XamlExplorerHostIslandWindow",   // Win11 Start, search, quick settings
    L"MultitaskingViewFrame",          // Task View, Alt+Tab
    L"NotifyIconOverflowWindow",       // Win10 tray overflow
    L"TopLevelWindowForOverflowXamlIsland",  // Win11 tray overflow
};

constexpr UINT_PTR kToolId = 1;

std::wstring ClassOf(HWND hwnd) {
    wchar_t name[256];
    const int n = GetClassNameW(hwnd, name, static_cast<int>(std::size(name)));
    return std::wstring(name, n > 0 ? static_cast<size_t>(n) : 0);
}

Rect WindowRect(HWND hwnd) {
    RECT r{};
    GetWindowRect(hwnd, &r);
    return {r.left, r.top, r.right, r.bottom};
}

Rect MonitorRect(HMONITOR monitor) {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    return {mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom};
}

Rect Offset(const Rect& r, int dx, int dy) { return {r.left + dx, r.top + dy, r.right + dx, r.bottom + dy}; }

int Margin(unsigned dpi) { return MulDiv(4, static_cast<int>(dpi), 96); }  // as in PlaceOverlay

// A maximized window with a title bar: an ordinary app even when, with an auto-hidden taskbar,
// it covers the whole monitor. Fullscreen video and games drop the caption.
bool IsMaximizedWithCaption(HWND hwnd) {
    return IsZoomed(hwnd) && (GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
}

bool StillFullscreen(HWND hwnd, HMONITOR monitor) {
    return IsWindow(hwnd) && IsWindowVisible(hwnd) && !IsIconic(hwnd) &&
           MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL) == monitor &&
           IsFullscreenOnMonitor(WindowRect(hwnd), MonitorRect(monitor), false, IsMaximizedWithCaption(hwnd));
}

// True if `upper` is above `lower` in the z-order.
bool IsAbove(HWND upper, HWND lower) {
    int guard = 0;
    for (HWND h = GetWindow(lower, GW_HWNDPREV); h && guard < 10000; h = GetWindow(h, GW_HWNDPREV), ++guard) {
        if (h == upper) return true;
    }
    return false;
}

}  // namespace

bool IsShellWindowClass(std::wstring_view cls) {
    return std::find(kShellClasses.begin(), kShellClasses.end(), cls) != kShellClasses.end();
}

bool IsShellFlyoutClass(std::wstring_view cls) {
    return std::find(kFlyoutClasses.begin(), kFlyoutClasses.end(), cls) != kFlyoutClasses.end();
}

bool WantsOverlay(bool primary, bool trayFound, bool allTaskbars) {
    return primary ? trayFound : allTaskbars;
}

OverlayManager::OverlayManager() = default;
OverlayManager::~OverlayManager() {
    Shutdown();
    if (teardownPending_) Teardown();
}

bool OverlayManager::Init(HINSTANCE inst, HWND appWnd) {
    if (initialized_ || instance_) return false;
    inst_ = inst;
    appWnd_ = appWnd;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    clickMsg_ = RegisterWindowMessageW(L"NetworkSpeedMonitor.OverlayClick");
    light_ = IsTaskbarLight();
    instance_ = this;
    initialized_ = true;
    foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                      &OverlayManager::OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
    Run(kRebuild);
    return true;
}

void OverlayManager::Shutdown() {
    if (!initialized_) return;
    initialized_ = false;  // Run and every Execute step stop here, including frames below us
    if (foregroundHook_) UnhookWinEvent(foregroundHook_);
    foregroundHook_ = nullptr;
    if (locationHook_) UnhookWinEvent(locationHook_);
    locationHook_ = nullptr;
    space_.Restore();  // now (or right after a write to Explorer that is in flight)
    if (busy_) {
        // Re-entered from a pumping call (UIA, a cross-process SetWindowPos) inside Run: frames
        // below still use bars_, so destroy the windows once Run unwinds.
        teardownPending_ = true;
        return;
    }
    Teardown();
}

void OverlayManager::Teardown() {
    teardownPending_ = false;
    DestroyBars();
    instance_ = nullptr;
}

void OverlayManager::SetOptions(const OverlayOptions& o) {
    const bool rebuild = o.allTaskbars != options_.allTaskbars;
    options_ = o;
    if (initialized_) Run(rebuild ? kRebuild : kPlace);
}

void OverlayManager::Update(std::optional<Speed> speed, const SpeedHistory& history) {
    speed_ = speed;
    history_ = &history;
    if (!initialized_) return;
    ++ticksSinceProbe_;

    const bool barsAlive = std::all_of(bars_.begin(), bars_.end(), [](const auto& b) { return IsWindow(b->info.hwnd); });
    const TickPlan plan = PlanTick(FindWindowW(L"Shell_TrayWnd", nullptr) != taskbar_ || !barsAlive, AnyShown(),
                                   probeDue_, ticksSinceProbe_, taskbar_ != nullptr, !bars_.empty());
    unsigned work = kShell | kZOrder;
    if (plan.rebuild) work |= kRebuild;
    if (plan.fullProbe) work |= kFullProbe;
    if (plan.lightProbe) work |= kLightProbe;
    if (plan.followTaskbars) work |= kTaskbarMoved;
    Run(work);
}

TickPlan PlanTick(bool explorerChanged, bool anyShown, bool probeDue, unsigned ticksSinceProbe, bool haveTaskbar,
                  bool haveBars) {
    TickPlan p;
    if (explorerChanged) {
        p.rebuild = true;  // Explorer exited or restarted (TaskbarCreated covers the restart too)
        return p;
    }
    if (anyShown && (probeDue || ticksSinceProbe >= 2)) {
        p.fullProbe = true;  // the UIA extent is refreshed every 2 s, and only while an overlay is shown
    } else if (haveTaskbar && !haveBars && ticksSinceProbe >= 2) {
        p.lightProbe = true;  // nothing to show yet (e.g. no tray): watch for a taskbar that qualifies
    }
    p.followTaskbars = haveBars;  // catches the auto-hide slide, which raises no location event
    return p;
}

void OverlayManager::SetTooltip(const std::wstring& text) {
    tooltip_ = text;
    for (const auto& bar : bars_) {
        if (!bar->tooltip) continue;
        TTTOOLINFOW ti{sizeof(ti)};
        ti.hwnd = bar->window.Hwnd();
        ti.uId = kToolId;
        ti.lpszText = tooltip_.data();
        SendMessageW(bar->tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
}

bool OverlayManager::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (!initialized_) return false;
    if (taskbarCreatedMsg_ && msg == taskbarCreatedMsg_) {
        Run(kRebuild);
        return true;
    }
    if (clickMsg_ && msg == clickMsg_) {
        if (wp == 0) {
            if (onLeftClick) onLeftClick();
        } else if (onRightClick) {
            onRightClick(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        }
        return true;
    }
    switch (msg) {
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            Run(kRebuild);
            return false;
        case WM_SETTINGCHANGE:
            if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) {
                light_ = IsTaskbarLight();
                Run(kShell);  // any work renders
            }
            return false;
        case WM_ENDSESSION:
            if (wp) space_.Restore();
            return false;
        default:
            return false;
    }
}

void CALLBACK OverlayManager::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD,
                                         DWORD) {
    OverlayManager* self = instance_;
    if (!self) return;
    if (event == EVENT_SYSTEM_FOREGROUND) {
        self->Run(kForeground);
        return;
    }
    if (event != EVENT_OBJECT_LOCATIONCHANGE || idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd) return;
    if (self->busy_) {  // bars_ may be mid-rebuild: queue every location check instead of looking
        self->Run(kTaskbarMoved | kTrayMoved | kSpace);
        return;
    }
    if (hwnd == self->space_.TaskSwitch()) {
        self->Run(kSpace);
    } else if (hwnd == self->space_.Rebar()) {
        self->Run(kSpace | kTrayMoved);
    } else if (hwnd == self->trayNotify_) {
        self->Run(kTrayMoved);
    } else if (std::any_of(self->bars_.begin(), self->bars_.end(),
                           [hwnd](const auto& b) { return b->info.hwnd == hwnd; })) {
        self->Run(kTaskbarMoved);
    }
}

void OverlayManager::Run(unsigned work) {
    pending_ |= work;
    if (busy_ || !initialized_) return;
    busy_ = true;
    while (pending_ && initialized_) {
        const unsigned w = pending_;
        pending_ = 0;
        Execute(w);
    }
    pending_ = 0;
    busy_ = false;
    if (teardownPending_) Teardown();  // Shutdown arrived while we were busy
}

void OverlayManager::Execute(unsigned w) {
    if (w & kRebuild) {
        Rebuild();
        return;
    }
    // The periodic probes double as a check that the Win10 reservation is still in place (a
    // GetWindowRect unless Explorer changed it), in case a location event was missed.
    if (w & (kSpace | kFullProbe | kLightProbe)) space_.OnLocationChange(space_.TaskSwitch());
    if (!initialized_) return;  // every step below can pump messages and so meet a Shutdown
    if (w & kFullProbe) {
        Probe(FullProbeMode());
    } else if (w & kLightProbe) {
        Probe(OccupiedProbe::None);
    } else {
        if (w & kTaskbarMoved) FollowTaskbars();
        if (initialized_ && (w & kTrayMoved) && TrayChanged()) Probe(OccupiedProbe::None);
    }
    if (!initialized_) return;
    if (w & kPlace) Place();
    if (!initialized_) return;
    if (w & (kShell | kForeground)) EvaluateShell();
    Render();
    // Re-assert topmost when the foreground changes (clicking the taskbar raises it over us),
    // or when the taskbar is found above an overlay; never while a flyout is in front, and
    // overlays on a monitor with a fullscreen app are hidden, so they are never raised over it.
    if (!flyoutOpen_ && (w & (kForeground | kZOrder))) RaiseShown(!(w & kForeground));
}

void OverlayManager::Rebuild() {
    DestroyBars();
    taskbar_ = FindWindowW(L"Shell_TrayWnd", nullptr);
    ticksSinceProbe_ = 0;
    if (!taskbar_) return;
    trayNotify_ = FindWindowExW(taskbar_, nullptr, L"TrayNotifyWnd", nullptr);
    if (!IsWindows11()) space_.Attach(taskbar_);

    const std::vector<TaskbarInfo> probed = ProbeTaskbars(FullProbeMode());
    if (!initialized_) return;  // Shutdown ran while UIA pumped messages; it restored the space
    for (const TaskbarInfo& info : probed) {
        if (!WantsOverlay(info.primary, info.trayFound, options_.allTaskbars)) continue;
        auto bar = std::make_unique<Bar>();
        bar->info = info;
        if (!bar->window.Create(inst_, appWnd_)) continue;
        SetWindowTextW(bar->window.Hwnd(), LoadStr(IDS_APP_NAME).c_str());
        bar->window.onLeftClick = [this] { PostMessageW(appWnd_, clickMsg_, 0, 0); };
        bar->window.onRightClick = [this](POINT pt) { PostMessageW(appWnd_, clickMsg_, 1, MAKELPARAM(pt.x, pt.y)); };
        CreateTooltip(*bar);
        bars_.push_back(std::move(bar));
    }

    DWORD explorerPid = 0;
    GetWindowThreadProcessId(taskbar_, &explorerPid);
    // Covers the auto-hide slide, taskbar moves and resizes, and the tray and task list
    // changing size. Explorer only: hooking every process would wake us on every caret move.
    locationHook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
                                    &OverlayManager::OnWinEvent, explorerPid, 0, WINEVENT_OUTOFCONTEXT);
    Place();
    EvaluateShell();
    Render();
    probeDue_ = false;
}

void OverlayManager::DestroyBars() {
    if (locationHook_) UnhookWinEvent(locationHook_);
    locationHook_ = nullptr;
    space_.Restore();
    bars_.clear();
    taskbar_ = nullptr;
    trayNotify_ = nullptr;
    wasShown_ = false;
}

OccupiedProbe OverlayManager::FullProbeMode() const {
    // With the Win10 reservation the task list can't reach the overlay: no UIA for the primary.
    return space_.Attached() ? OccupiedProbe::SecondaryOnly : OccupiedProbe::All;
}

void OverlayManager::Probe(OccupiedProbe occupied) {
    ticksSinceProbe_ = 0;
    probeDue_ = false;
    const std::vector<TaskbarInfo> probed = ProbeTaskbars(occupied);
    if (!initialized_) return;  // Shutdown ran while UIA pumped messages
    std::vector<TaskbarInfo> wanted;
    for (const TaskbarInfo& info : probed) {
        if (WantsOverlay(info.primary, info.trayFound, options_.allTaskbars)) wanted.push_back(info);
    }
    const bool sameSet = wanted.size() == bars_.size() &&
                         std::equal(wanted.begin(), wanted.end(), bars_.begin(),
                                    [](const TaskbarInfo& i, const auto& b) { return i.hwnd == b->info.hwnd; });
    if (!sameSet) {
        Rebuild();  // a taskbar appeared, went away, or lost its tray
        return;
    }
    for (size_t i = 0; i < wanted.size(); ++i) {
        Bar& bar = *bars_[i];
        const bool measured = occupied == OccupiedProbe::All ||
                              (occupied == OccupiedProbe::SecondaryOnly && !bar.info.primary);
        const bool reserved = bar.info.primary && space_.Attached();
        if (!measured && !reserved) {  // keep the last UIA extent, moved with the taskbar
            const TaskbarGeometry& old = bar.info.geom;
            wanted[i].geom.occupied = Offset(old.occupied, wanted[i].geom.taskbar.left - old.taskbar.left,
                                             wanted[i].geom.taskbar.top - old.taskbar.top);
        }
        bar.info = wanted[i];
    }
    Place();
}

void OverlayManager::FollowTaskbars() {
    for (const auto& bar : bars_) {
        TaskbarGeometry& g = bar->info.geom;
        const Rect now = WindowRect(bar->info.hwnd);
        if (now == g.taskbar) continue;
        if (now.Width() != g.taskbar.Width() || now.Height() != g.taskbar.Height() ||
            MonitorFromWindow(bar->info.hwnd, MONITOR_DEFAULTTONEAREST) != bar->info.monitor) {
            Probe(FullProbeMode());  // resized, moved to another edge or monitor, or a DPI change
            return;  // bars_ may have been rebuilt
        }
        // Same size, new position: the auto-hide slide. Move everything with it; no probe.
        const int dx = now.left - g.taskbar.left, dy = now.top - g.taskbar.top;
        g.taskbar = now;
        g.tray = Offset(g.tray, dx, dy);
        g.occupied = Offset(g.occupied, dx, dy);
        bar->placement.rect = Offset(bar->placement.rect, dx, dy);
    }
}

bool OverlayManager::TrayChanged() const {
    if (!trayNotify_ || !IsWindow(trayNotify_)) return false;
    for (const auto& bar : bars_) {
        if (!bar->info.primary) continue;
        const TaskbarGeometry& g = bar->info.geom;
        const Rect now = WindowRect(trayNotify_);
        const Rect tb = WindowRect(bar->info.hwnd);
        // Compare relative to the taskbar so the auto-hide slide doesn't count as a change.
        return Offset(now, -tb.left, -tb.top) != Offset(g.tray, -g.taskbar.left, -g.taskbar.top);
    }
    return true;  // no primary overlay yet: let the probe decide
}

void OverlayManager::Place() {
    const Bar* primary = nullptr;
    for (const auto& bar : bars_) {
        const TaskbarGeometry& g = bar->info.geom;
        if (!bar->sizes || bar->sizesDpi != g.dpi || bar->sizesLetters != options_.letters) {
            bar->sizes = MeasureOverlay(g.dpi, options_.letters);
            bar->sizesDpi = g.dpi;
            bar->sizesLetters = options_.letters;
        }
        bar->placement = PlaceOverlay(g, *bar->sizes, options_.showGraph);
        if (bar->info.primary) primary = bar.get();
    }
    if (!space_.Attached() || !initialized_) return;
    if (primary && primary->placement.visible) {
        const TaskbarGeometry& g = primary->info.geom;
        space_.Reserve(primary->placement.rect, g.edge, Margin(g.dpi));
    } else {
        space_.Reserve({}, Edge::Bottom, 0);  // nothing to make room for: natural size
    }
}

void OverlayManager::EvaluateShell() {
    std::erase_if(fullscreen_, [](const auto& e) { return !StillFullscreen(e.second, e.first); });
    flyoutOpen_ = false;
    HWND fg = GetForegroundWindow();
    if (!fg) return;  // e.g. mid-switch or a secure desktop: keep what we know
    const std::wstring cls = ClassOf(fg);
    if (IsShellFlyoutClass(cls)) {
        flyoutOpen_ = true;
        return;
    }
    HMONITOR monitor = MonitorFromWindow(fg, MONITOR_DEFAULTTONULL);
    if (!monitor) return;
    std::erase_if(fullscreen_, [monitor](const auto& e) { return e.first == monitor; });
    if (IsFullscreenOnMonitor(WindowRect(fg), MonitorRect(monitor), IsShellWindowClass(cls), IsMaximizedWithCaption(fg)))
        fullscreen_.emplace_back(monitor, fg);
}

bool OverlayManager::Gated(const Bar& bar) const {
    const TaskbarGeometry& g = bar.info.geom;
    if (flyoutOpen_ || !IsTaskbarShown(g.taskbar, g.monitor, g.edge)) return true;
    return std::any_of(fullscreen_.begin(), fullscreen_.end(),
                       [&](const auto& e) { return e.first == bar.info.monitor; });
}

void OverlayManager::Render() {
    for (const auto& bar : bars_) RenderBar(*bar);
    const bool shown = AnyShown();
    if (shown && !wasShown_) probeDue_ = true;  // geometry may have changed while hidden
    wasShown_ = shown;
}

void OverlayManager::RenderBar(Bar& bar) {
    if (!bar.placement.visible || Gated(bar)) {
        if (bar.window.Visible() && bar.tooltip) SendMessageW(bar.tooltip, TTM_POP, 0, 0);
        bar.window.Hide();
        return;
    }
    const OverlayContent content{speed_,   options_.units, options_.letters,  bar.placement.showGraph,
                                 history_, light_,         bar.info.geom.dpi, bar.placement.compact};
    bar.window.Show(bar.placement, content);

    const SIZE size{bar.placement.rect.Width(), bar.placement.rect.Height()};
    if (bar.tooltip && (size.cx != bar.toolSize.cx || size.cy != bar.toolSize.cy)) {
        TTTOOLINFOW ti{sizeof(ti)};
        ti.hwnd = bar.window.Hwnd();
        ti.uId = kToolId;
        ti.rect = {0, 0, size.cx, size.cy};
        SendMessageW(bar.tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
        bar.toolSize = size;
    }
}

void OverlayManager::RaiseShown(bool onlyIfCovered) {
    for (const auto& bar : bars_) {
        if (!bar->window.Visible()) continue;
        if (onlyIfCovered && !IsAbove(bar->info.hwnd, bar->window.Hwnd())) continue;
        bar->window.RaiseTopmost();
    }
}

bool OverlayManager::AnyShown() const {
    return std::any_of(bars_.begin(), bars_.end(), [](const auto& b) { return b->window.Visible(); });
}

void OverlayManager::CreateTooltip(Bar& bar) {
    bar.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, bar.window.Hwnd(),
                                  nullptr, inst_, nullptr);
    if (!bar.tooltip) return;
    TTTOOLINFOW ti{sizeof(ti)};
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = bar.window.Hwnd();
    ti.uId = kToolId;
    ti.lpszText = tooltip_.data();
    SendMessageW(bar.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

}  // namespace nsm
