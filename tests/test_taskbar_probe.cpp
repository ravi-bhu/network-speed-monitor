#include <windows.h>
#include <objbase.h>

#include "doctest.h"
#include "ui/taskbar_probe.h"

using namespace nsm;

TEST_CASE("EdgeFromRects picks the monitor edge the taskbar hugs") {
    const Rect mon{0, 0, 1920, 1080};
    CHECK(EdgeFromRects({0, 1040, 1920, 1080}, mon) == Edge::Bottom);
    CHECK(EdgeFromRects({0, 0, 1920, 40}, mon) == Edge::Top);
    CHECK(EdgeFromRects({0, 0, 62, 1080}, mon) == Edge::Left);
    CHECK(EdgeFromRects({1858, 0, 1920, 1080}, mon) == Edge::Right);
}

TEST_CASE("EdgeFromRects works on a monitor that is not at the origin") {
    const Rect mon{-1280, 200, 0, 1224};
    CHECK(EdgeFromRects({-1280, 1184, 0, 1224}, mon) == Edge::Bottom);
    CHECK(EdgeFromRects({-1280, 200, 0, 240}, mon) == Edge::Top);
    CHECK(EdgeFromRects({-1280, 200, -1218, 1224}, mon) == Edge::Left);
    CHECK(EdgeFromRects({-62, 200, 0, 1224}, mon) == Edge::Right);
}

TEST_CASE("FallbackTray is a zero-size rect at the taskbar's far end") {
    const Rect h{0, 1040, 1920, 1080};
    CHECK(FallbackTray(h, Edge::Bottom) == Rect{1920, 1040, 1920, 1080});
    CHECK(FallbackTray(h, Edge::Top) == Rect{1920, 1040, 1920, 1080});
    const Rect v{0, 0, 62, 1080};
    CHECK(FallbackTray(v, Edge::Left) == Rect{0, 1080, 62, 1080});
    CHECK(FallbackTray(v, Edge::Right) == Rect{0, 1080, 62, 1080});
}

TEST_CASE("OccupiedExtent unions buttons entirely before the tray (horizontal)") {
    const Rect taskbar{0, 1040, 1920, 1080};
    const Rect tray{1700, 1040, 1860, 1080};
    const std::vector<Rect> buttons{
        {0, 1040, 48, 1080},       // Start
        {48, 1040, 100, 1080},     // task button
        {400, 1040, 460, 1080},    // task button
        {1710, 1044, 1734, 1076},  // tray icon: inside the tray, excluded
        {1680, 1040, 1720, 1080},  // straddles tray.left: excluded
        {1860, 1040, 1920, 1080},  // right of the tray: excluded
        {500, 1040, 500, 1080},    // empty: excluded
        {600, 0, 700, 40},         // not on this taskbar (scrolled out / other monitor): excluded
    };
    CHECK(OccupiedExtent(buttons, taskbar, tray, Edge::Bottom) == Rect{0, 1040, 460, 1080});
}

TEST_CASE("OccupiedExtent unions buttons entirely above the tray (vertical)") {
    const Rect taskbar{0, 0, 62, 1080};
    const Rect tray{0, 900, 62, 1040};
    const std::vector<Rect> buttons{
        {0, 0, 62, 40},
        {0, 40, 62, 300},
        {0, 950, 62, 980},    // inside the tray
        {0, 1040, 62, 1080},  // below the tray
    };
    CHECK(OccupiedExtent(buttons, taskbar, tray, Edge::Left) == Rect{0, 0, 62, 300});
}

TEST_CASE("OccupiedExtent is empty when no button qualifies") {
    const Rect taskbar{0, 1040, 1920, 1080};
    const Rect tray{1700, 1040, 1860, 1080};
    CHECK(OccupiedExtent({}, taskbar, tray, Edge::Bottom).Empty());
    CHECK(OccupiedExtent({{1710, 1040, 1730, 1080}}, taskbar, tray, Edge::Bottom).Empty());
}

namespace {

bool InsideOrOn(const Rect& inner, const Rect& outer) {
    return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
           inner.bottom <= outer.bottom;
}

}  // namespace

TEST_CASE("ProbeTaskbars finds the primary taskbar" * doctest::test_suite("integration")) {
    if (!FindWindowW(L"Shell_TrayWnd", nullptr)) {
        MESSAGE("No Shell_TrayWnd in this session (e.g. a CI service session); skipping.");
        return;
    }
    // ProbeTaskbars assumes per-monitor v2 awareness (the exe's manifest) and an STA for UIA.
    // COM is deliberately left initialized: the IUIAutomation instance is cached for the process.
    const DPI_AWARENESS_CONTEXT prev = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    REQUIRE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    const auto bars = ProbeTaskbars();
    SetThreadDpiAwarenessContext(prev);

    REQUIRE(!bars.empty());
    CHECK(bars.front().primary);
    int primaries = 0;
    for (const auto& b : bars) {
        primaries += b.primary ? 1 : 0;
        const TaskbarGeometry& g = b.geom;
        CHECK(b.hwnd != nullptr);
        CHECK(b.monitor != nullptr);
        CHECK(!g.taskbar.Empty());
        CHECK(g.dpi >= 96);
        CHECK(InsideOrOn(g.tray, g.taskbar));
        if (!g.occupied.Empty()) {
            if (g.edge == Edge::Bottom || g.edge == Edge::Top) CHECK(g.occupied.right <= g.tray.left);
            else CHECK(g.occupied.bottom <= g.tray.top);
        }
    }
    CHECK(primaries == 1);

    const TaskbarGeometry& pg = bars.front().geom;
    MESSAGE("primary: taskbar ", pg.taskbar.left, ",", pg.taskbar.top, ",", pg.taskbar.right, ",", pg.taskbar.bottom,
            "  tray ", pg.tray.left, ",", pg.tray.top, ",", pg.tray.right, ",", pg.tray.bottom, "  occupied ",
            pg.occupied.left, ",", pg.occupied.top, ",", pg.occupied.right, ",", pg.occupied.bottom, "  dpi ", pg.dpi,
            "  edge ", static_cast<int>(pg.edge), "  taskbars ", bars.size());
    CHECK(!pg.tray.Empty());      // TrayNotifyWnd on the primary taskbar
    CHECK(!pg.occupied.Empty());  // UIA reports at least the Start button
}

TEST_CASE("IsWindows11 agrees with the registry build number" * doctest::test_suite("integration")) {
    wchar_t build[32]{};
    DWORD size = sizeof(build);
    REQUIRE(RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                         L"CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, build, &size) == ERROR_SUCCESS);
    CHECK(IsWindows11() == (_wtoi(build) >= 22000));
}
