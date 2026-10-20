#pragma once
#include <windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/overlay_layout.h"
#include "core/speed_history.h"
#include "core/types.h"
#include "ui/taskbar_probe.h"
#include "ui/taskbar_space_win10.h"

namespace nsm {

class OverlayWindow;

struct OverlayOptions {
    bool allTaskbars = false;
    bool showGraph = true;
    bool letters = false;
    UnitMode units = UnitMode::Bytes;
};

// Keeps one overlay per taskbar in use (the primary, or all of them) correct across Explorer
// restarts, taskbar moves and auto-hide, fullscreen apps, shell flyouts, display and DPI
// changes, and theme switches. On Windows 10 it also reserves the overlay's space in the task
// list (TaskbarSpaceWin10). Single instance per process (the WinEvent hooks are static), used
// from the UI thread only; that thread needs an STA (UI Automation) and a message loop.
class OverlayManager {
public:
    OverlayManager();  // out of line: Bar is incomplete here
    ~OverlayManager();
    OverlayManager(const OverlayManager&) = delete;
    OverlayManager& operator=(const OverlayManager&) = delete;

    // appWnd: a hidden top-level window (not HWND_MESSAGE, so it gets the TaskbarCreated and
    // WM_SETTINGCHANGE broadcasts) that forwards its messages to HandleMessage.
    bool Init(HINSTANCE inst, HWND appWnd);
    void SetOptions(const OverlayOptions& o);
    void Update(std::optional<Speed> speed, const SpeedHistory& history);  // called every tick
    void SetTooltip(const std::wstring& text);
    // True only for messages that belong to the manager alone (TaskbarCreated, its own rebuild
    // request). WM_DISPLAYCHANGE, WM_DPICHANGED, WM_SETTINGCHANGE and WM_ENDSESSION are acted on
    // but return false, so the app can still handle them and pass them to DefWindowProc.
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    // Restores the Win10 task list space. Safe to call re-entrantly (e.g. WM_ENDSESSION or
    // WM_CLOSE delivered while a probe pumps messages): no work runs after it, and the
    // overlay windows are destroyed once the interrupted work unwinds.
    void Shutdown();

    std::function<void()> onLeftClick;
    std::function<void(POINT)> onRightClick;

private:
    struct Bar;
    enum Work : unsigned {
        kRebuild = 1u << 0,        // destroy every overlay, re-probe, recreate
        kFullProbe = 1u << 1,      // re-probe including the UIA occupied extent
        kLightProbe = 1u << 2,     // re-probe without UIA
        kTaskbarMoved = 1u << 3,   // a taskbar window moved (auto-hide slide) or was resized
        kTrayMoved = 1u << 4,      // TrayNotifyWnd or ReBarWindow32 moved: maybe the tray changed size
        kSpace = 1u << 5,          // the Win10 task list was resized: re-apply the reservation
        kPlace = 1u << 6,          // options changed: re-measure and re-place
        kShell = 1u << 7,          // re-evaluate the foreground window (fullscreen, flyouts)
        kForeground = 1u << 8,     // kShell, then re-assert topmost (foreground changed)
        kZOrder = 1u << 9,         // re-assert topmost only where the taskbar covers an overlay
    };

    static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                    DWORD thread, DWORD time);

    // Runs `work` now, or queues it when called re-entrantly: UIA and cross-process window
    // calls pump messages, so hooks and timers can fire in the middle of a probe.
    void Run(unsigned work);
    void Execute(unsigned work);

    void Rebuild();
    void EnsureHooks();  // installs whichever WinEvent hook is missing (none yet, or creation failed)
    void DestroyBars();
    void Teardown();  // DestroyBars and release the static instance
    void Probe(OccupiedProbe occupied);
    void FollowTaskbars();
    bool TrayChanged() const;
    void Place();
    void EvaluateShell();
    void Render();
    void RenderBar(Bar& bar);
    void RaiseShown(bool onlyIfCovered);
    bool Gated(const Bar& bar) const;  // hidden by auto-hide, a fullscreen app or a flyout
    bool AnyShown() const;
    void CreateTooltip(Bar& bar);
    OccupiedProbe FullProbeMode() const;

    static inline OverlayManager* instance_ = nullptr;

    HINSTANCE inst_ = nullptr;
    HWND appWnd_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    UINT clickMsg_ = 0;  // overlay clicks are posted to appWnd_ so callbacks run outside the overlay's WndProc
    bool initialized_ = false;

    OverlayOptions options_;
    std::wstring tooltip_;
    std::optional<Speed> speed_;
    SpeedHistory emptyHistory_;
    const SpeedHistory* history_ = &emptyHistory_;
    bool light_ = false;

    std::vector<std::unique_ptr<Bar>> bars_;
    // The taskbars Rebuild wanted an overlay on, including any whose window couldn't be created:
    // a probe that finds the same set doesn't rebuild (a failed one is retried at the next rebuild).
    std::vector<HWND> wanted_;
    HWND taskbar_ = nullptr;  // the Shell_TrayWnd the overlays were built for
    HWND trayNotify_ = nullptr;
    TaskbarSpaceWin10 space_;
    HWINEVENTHOOK locationHook_ = nullptr;
    HWINEVENTHOOK foregroundHook_ = nullptr;

    bool flyoutOpen_ = false;
    std::vector<std::pair<HMONITOR, HWND>> fullscreen_;  // monitor -> the fullscreen window in front there
    bool wasShown_ = false;
    bool probeDue_ = false;
    unsigned ticksSinceProbe_ = 0;

    bool busy_ = false;
    unsigned pending_ = 0;
    bool teardownPending_ = false;  // Shutdown ran inside Run; destroy the overlays when Run unwinds
};

// Pure rules behind the manager (exposed for tests).
bool IsShellWindowClass(std::wstring_view cls);  // desktop and taskbars: never a fullscreen app
bool IsShellFlyoutClass(std::wstring_view cls);  // Start, search, Task View, tray overflow: hide while in front
// Whether a probed taskbar gets an overlay. The primary needs its real tray: without
// TrayNotifyWnd the fallback would put the overlay over the tray icons and clock.
bool WantsOverlay(bool primary, bool trayFound, bool allTaskbars);
// Whether two probes found the same taskbars, in any order: secondary taskbars are enumerated in
// z-order, which changes whenever one is clicked.
bool SameTaskbars(std::vector<HWND> a, std::vector<HWND> b);
// What the 1 s tick does. followTaskbars re-reads each taskbar's rect (one GetWindowRect, no
// UIA) every tick: Explorer doesn't raise a location event for the auto-hide slide, so a
// hidden overlay would otherwise never notice the taskbar coming back.
struct TickPlan {
    bool rebuild = false, fullProbe = false, lightProbe = false, followTaskbars = false;
};
TickPlan PlanTick(bool explorerChanged, bool anyShown, bool probeDue, unsigned ticksSinceProbe, bool haveTaskbar,
                  bool haveBars);

}  // namespace nsm
