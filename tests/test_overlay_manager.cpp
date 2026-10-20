#include "doctest.h"

#include "ui/overlay_manager.h"
#include "ui/overlay_window.h"

using namespace nsm;

TEST_CASE("IsShellWindowClass: the desktop and the taskbars are never fullscreen apps") {
    CHECK(IsShellWindowClass(L"Progman"));
    CHECK(IsShellWindowClass(L"WorkerW"));
    CHECK(IsShellWindowClass(L"Shell_TrayWnd"));
    CHECK(IsShellWindowClass(L"Shell_SecondaryTrayWnd"));
    CHECK(!IsShellWindowClass(L"Chrome_WidgetWin_1"));
    CHECK(!IsShellWindowClass(L"progman"));  // class names are matched exactly
    CHECK(!IsShellWindowClass(L""));
}

TEST_CASE("IsShellFlyoutClass: Start, search, Task View and tray overflow hide the overlay") {
    CHECK(IsShellFlyoutClass(L"Windows.UI.Core.CoreWindow"));
    CHECK(IsShellFlyoutClass(L"XamlExplorerHostIslandWindow"));
    CHECK(IsShellFlyoutClass(L"MultitaskingViewFrame"));
    CHECK(IsShellFlyoutClass(L"NotifyIconOverflowWindow"));
    CHECK(IsShellFlyoutClass(L"TopLevelWindowForOverflowXamlIsland"));
    CHECK(!IsShellFlyoutClass(L"Shell_TrayWnd"));
    CHECK(!IsShellFlyoutClass(L"ApplicationFrameWindow"));
    CHECK(!IsShellFlyoutClass(L"Windows.UI.Core.CoreWindow2"));
    CHECK(!IsShellFlyoutClass(L""));
}

TEST_CASE("WantsOverlay: the primary taskbar needs its real tray") {
    CHECK(WantsOverlay(true, true, false));
    CHECK(WantsOverlay(true, true, true));
    CHECK(!WantsOverlay(true, false, false));  // no TrayNotifyWnd: hide rather than cover the tray
    CHECK(!WantsOverlay(true, false, true));
}

TEST_CASE("WantsOverlay: secondary taskbars only with allTaskbars, clock fallback allowed") {
    CHECK(!WantsOverlay(false, true, false));
    CHECK(!WantsOverlay(false, false, false));
    CHECK(WantsOverlay(false, true, true));
    CHECK(WantsOverlay(false, false, true));
}

TEST_CASE("PlanTick: a hidden overlay still follows the taskbar every tick (#31)") {
    // Auto-hide slid the taskbar away: nothing is shown, the bars exist, no probe is due.
    const TickPlan p = PlanTick(false, false, false, 1, true, true);
    CHECK(p.followTaskbars);
    CHECK(!p.rebuild);
    CHECK(!p.fullProbe);
}

TEST_CASE("PlanTick: rebuild when Explorer changed; probes on their cadence") {
    CHECK(PlanTick(true, true, false, 5, true, true).rebuild);
    const TickPlan shown = PlanTick(false, true, false, 2, true, true);
    CHECK(shown.fullProbe);
    const TickPlan noBars = PlanTick(false, false, false, 2, true, false);
    CHECK(noBars.lightProbe);
    CHECK(!noBars.followTaskbars);
}

TEST_CASE("ClickTracker: a button-up is a click only after a button-down on the overlay (#6)") {
    ClickTracker t;
    CHECK(!t.Up(MouseButton::Left, true));  // a drag that started elsewhere ends over the overlay
    CHECK(!t.Up(MouseButton::Right, true));

    t.Down(MouseButton::Left);
    CHECK(t.Pending());
    CHECK(t.Up(MouseButton::Left, true));
    CHECK(!t.Pending());
    CHECK(!t.Up(MouseButton::Left, true));  // one down, one click

    t.Down(MouseButton::Right);
    CHECK(t.Up(MouseButton::Right, true));
}

TEST_CASE("ClickTracker: released off the overlay, other button, or capture lost is no click (#6)") {
    ClickTracker t;
    t.Down(MouseButton::Left);
    CHECK(!t.Up(MouseButton::Left, false));  // pressed on the overlay, released elsewhere
    CHECK(!t.Pending());

    t.Down(MouseButton::Left);
    CHECK(!t.Up(MouseButton::Right, true));  // the other button does not complete the click
    CHECK(t.Pending());
    CHECK(t.Up(MouseButton::Left, true));

    t.Down(MouseButton::Right);
    t.Cancel();  // capture taken away mid-press
    CHECK(!t.Pending());
    CHECK(!t.Up(MouseButton::Right, true));
}
