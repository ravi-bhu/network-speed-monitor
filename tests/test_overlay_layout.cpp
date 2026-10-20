#include "doctest.h"

#include "core/overlay_layout.h"

using namespace nsm;

namespace {
TaskbarGeometry G(Edge edge, Rect occupied) {
    return {{0, 0, 1920, 1080}, {0, 1040, 1920, 1080}, {1700, 1040, 1920, 1080}, occupied, edge, 96};
}
}  // namespace

TEST_CASE("PlaceOverlay: room for graph sits left of tray with 4px margin") {
    auto p = PlaceOverlay(G(Edge::Bottom, {0, 1040, 1000, 1080}), {150, 90, 40}, true);
    CHECK(p.visible);
    CHECK(p.showGraph);
    CHECK(p.rect == Rect{1546, 1040, 1696, 1080});
}

TEST_CASE("PlaceOverlay: crowded drops graph, then hides") {
    auto a = PlaceOverlay(G(Edge::Bottom, {0, 1040, 1560, 1080}), {150, 90, 40}, true);
    CHECK(a.visible);
    CHECK(!a.showGraph);
    CHECK(a.rect == Rect{1606, 1040, 1696, 1080});
    auto b = PlaceOverlay(G(Edge::Bottom, {0, 1040, 1620, 1080}), {150, 90, 40}, true);
    CHECK(!b.visible);
}

TEST_CASE("PlaceOverlay: margin scales with DPI") {  // dpi 144 -> margin 6
    auto g = G(Edge::Bottom, {});
    g.dpi = 144;
    CHECK(PlaceOverlay(g, {150, 90, 40}, false).rect.right == 1694);
}

TEST_CASE("PlaceOverlay: vertical taskbar stacks above tray, text only when narrow") {
    TaskbarGeometry g{{0, 0, 1920, 1080}, {0, 0, 62, 1080}, {0, 900, 62, 1080}, {0, 0, 62, 400}, Edge::Left, 96};
    auto p = PlaceOverlay(g, {150, 54, 40}, true);
    CHECK(p.visible);
    CHECK(!p.showGraph);
    CHECK(p.rect == Rect{4, 856, 58, 896});
}

TEST_CASE("PlaceOverlay: vertical taskbar wide enough doubles height for graph") {
    TaskbarGeometry g{{0, 0, 1920, 1080}, {0, 0, 200, 1080}, {0, 900, 200, 1080}, {}, Edge::Left, 96};
    auto p = PlaceOverlay(g, {150, 54, 40}, true);
    CHECK(p.visible);
    CHECK(p.showGraph);
    CHECK(p.rect == Rect{4, 816, 196, 896});
}

TEST_CASE("PlaceOverlay: vertical taskbar hides when height does not fit") {
    TaskbarGeometry g{{0, 0, 1920, 1080}, {0, 0, 62, 1080}, {0, 900, 62, 1080}, {0, 0, 62, 880}, Edge::Left, 96};
    CHECK(!PlaceOverlay(g, {150, 54, 40}, true).visible);
}

TEST_CASE("IsTaskbarShown detects auto-hidden taskbar") {
    CHECK(IsTaskbarShown({0, 1040, 1920, 1080}, {0, 0, 1920, 1080}, Edge::Bottom));
    CHECK(!IsTaskbarShown({0, 1078, 1920, 1118}, {0, 0, 1920, 1080}, Edge::Bottom));
}

TEST_CASE("IsFullscreenOnMonitor: covers monitor, not maximized, not shell") {
    CHECK(IsFullscreenOnMonitor({0, 0, 1920, 1080}, {0, 0, 1920, 1080}, false));
    CHECK(!IsFullscreenOnMonitor({-8, -8, 1928, 1048}, {0, 0, 1920, 1080}, false));
    CHECK(!IsFullscreenOnMonitor({0, 0, 1920, 1080}, {0, 0, 1920, 1080}, true));
}

TEST_CASE("PlaceOverlay: default-width vertical taskbar falls back to the compact layout (#30)") {
    // Left taskbar 70 px wide at 96 dpi: 62 px usable. Normal text needs 72, compact 52.
    TaskbarGeometry g{{0, 0, 1920, 1080}, {0, 0, 70, 1080}, {0, 900, 70, 1080}, {}, Edge::Left, 96};
    OverlaySizes s{140, 72, 40};
    s.compactWidth = 52;
    auto p = PlaceOverlay(g, s, true);
    CHECK(p.visible);
    CHECK(p.compact);
    CHECK(!p.showGraph);
    CHECK(p.rect == Rect{4, 856, 66, 896});
}

TEST_CASE("PlaceOverlay: compact is only used when the normal text doesn't fit (#30)") {
    TaskbarGeometry g{{0, 0, 1920, 1080}, {0, 0, 90, 1080}, {0, 900, 90, 1080}, {}, Edge::Left, 96};
    OverlaySizes s{140, 72, 40};
    s.compactWidth = 52;
    auto p = PlaceOverlay(g, s, true);
    CHECK(p.visible);
    CHECK(!p.compact);
}

TEST_CASE("PlaceOverlay: too narrow even for compact hides; no compact size means no compact (#30)") {
    TaskbarGeometry g{{0, 0, 1920, 1080}, {0, 0, 50, 1080}, {0, 900, 50, 1080}, {}, Edge::Left, 96};
    OverlaySizes s{140, 72, 40};
    s.compactWidth = 52;
    CHECK(!PlaceOverlay(g, s, true).visible);
    TaskbarGeometry g70 = g;
    g70.taskbar.right = 70;
    g70.tray.right = 70;
    CHECK(!PlaceOverlay(g70, {140, 72, 40}, true).visible);  // compactWidth 0: no compact layout
}

TEST_CASE("PlaceOverlay: crowded horizontal taskbar tries compact before hiding (#30)") {
    OverlaySizes s{150, 90, 40};
    s.compactWidth = 60;
    auto p = PlaceOverlay(G(Edge::Bottom, {0, 1040, 1620, 1080}), s, true);  // 72 px free
    CHECK(p.visible);
    CHECK(p.compact);
    CHECK(p.rect == Rect{1636, 1040, 1696, 1080});
}

TEST_CASE("IsFullscreenOnMonitor: a maximized window over an auto-hidden taskbar is not fullscreen (#31)") {
    // With auto-hide on, a maximized app's rect covers the whole monitor (borders included).
    CHECK(!IsFullscreenOnMonitor({-8, -8, 1928, 1088}, {0, 0, 1920, 1080}, false, true));
    CHECK(IsFullscreenOnMonitor({-8, -8, 1928, 1088}, {0, 0, 1920, 1080}, false, false));  // borderless fullscreen
}

TEST_CASE("PlaceOverlay: short vertical taskbar with nothing on it hides instead of sticking out (#4)") {
    // Right taskbar 100 px tall with the tray in its bottom 70: 26 px above the tray for a 40 px readout.
    TaskbarGeometry g{{0, 0, 1920, 1080}, {1720, 500, 1920, 600}, {1720, 530, 1920, 600}, {}, Edge::Right, 96};
    CHECK(!PlaceOverlay(g, {150, 90, 40}, false).visible);
    g.taskbar.top = 480;  // 46 px: fits
    auto p = PlaceOverlay(g, {150, 90, 40}, false);
    CHECK(p.visible);
    CHECK(p.rect == Rect{1724, 486, 1916, 526});
}

TEST_CASE("PlaceOverlay: right taskbar stacks above the tray like a left one (#4)") {
    TaskbarGeometry g{{0, 0, 1920, 1080}, {1720, 0, 1920, 1080}, {1720, 900, 1920, 1080}, {1720, 0, 1920, 400},
                      Edge::Right, 96};
    auto p = PlaceOverlay(g, {150, 54, 40}, true);
    CHECK(p.visible);
    CHECK(p.showGraph);
    CHECK(p.rect == Rect{1724, 816, 1916, 896});
}

TEST_CASE("PlaceOverlay: top taskbar sits left of the tray, centred in the bar (#4)") {
    TaskbarGeometry g{{0, 0, 1920, 1080}, {0, 0, 1920, 48}, {1700, 0, 1920, 48}, {0, 0, 1000, 48}, Edge::Top, 96};
    auto p = PlaceOverlay(g, {150, 90, 40}, true);
    CHECK(p.visible);
    CHECK(p.showGraph);
    CHECK(p.rect == Rect{1546, 4, 1696, 44});
}

TEST_CASE("PlaceOverlay: a readout taller than a horizontal taskbar is clamped to its thickness (#4)") {
    // "Use small taskbar buttons": a 30 px bar under a 32 px readout.
    TaskbarGeometry g{{0, 0, 1920, 1080}, {0, 1050, 1920, 1080}, {1700, 1050, 1920, 1080}, {}, Edge::Bottom, 96};
    auto p = PlaceOverlay(g, {150, 90, 32}, false);
    CHECK(p.visible);
    CHECK(p.rect == Rect{1606, 1050, 1696, 1080});
    g.edge = Edge::Top;
    g.taskbar = {0, 0, 1920, 30};
    g.tray = {1700, 0, 1920, 30};
    CHECK(PlaceOverlay(g, {150, 90, 32}, false).rect == Rect{1606, 0, 1696, 30});
}

TEST_CASE("PlaceOverlay: an empty tray Rect{} places at the taskbar's far end, like FallbackTray (#4)") {
    // A monitor right of the primary, so a {0,0,0,0} tray is nowhere near the taskbar.
    TaskbarGeometry h{{1920, 0, 3840, 1080}, {1920, 1040, 3840, 1080}, {}, {}, Edge::Bottom, 96};
    auto a = PlaceOverlay(h, {150, 90, 40}, false);
    CHECK(a.visible);
    CHECK(a.rect == Rect{3746, 1040, 3836, 1080});
    TaskbarGeometry v{{1920, 0, 3840, 1080}, {1920, 0, 2120, 1080}, {}, {}, Edge::Left, 96};
    auto b = PlaceOverlay(v, {150, 90, 40}, false);
    CHECK(b.visible);
    CHECK(b.rect == Rect{1924, 1036, 2116, 1076});
}

TEST_CASE("IsTaskbarShown: vertical edges (#4)") {
    const Rect monitor{0, 0, 1920, 1080};
    CHECK(IsTaskbarShown({0, 0, 62, 1080}, monitor, Edge::Left));
    CHECK(!IsTaskbarShown({-60, 0, 2, 1080}, monitor, Edge::Left));  // auto-hidden: 2 px peek
    CHECK(IsTaskbarShown({1858, 0, 1920, 1080}, monitor, Edge::Right));
    CHECK(!IsTaskbarShown({1918, 0, 1980, 1080}, monitor, Edge::Right));
}

TEST_CASE("IsTaskbarShown: exactly half visible counts as shown (#4)") {
    const Rect monitor{0, 0, 1920, 1080};
    CHECK(IsTaskbarShown({0, 1060, 1920, 1100}, monitor, Edge::Bottom));   // 20 of 40
    CHECK(!IsTaskbarShown({0, 1061, 1920, 1101}, monitor, Edge::Bottom));  // 19 of 40
    CHECK(IsTaskbarShown({0, -20, 1920, 20}, monitor, Edge::Top));
    CHECK(!IsTaskbarShown({0, -21, 1920, 19}, monitor, Edge::Top));
    CHECK(IsTaskbarShown({-31, 0, 31, 1080}, monitor, Edge::Left));  // 31 of 62
    CHECK(!IsTaskbarShown({-32, 0, 30, 1080}, monitor, Edge::Left));
    CHECK(IsTaskbarShown({1889, 0, 1951, 1080}, monitor, Edge::Right));
    CHECK(!IsTaskbarShown({1890, 0, 1952, 1080}, monitor, Edge::Right));
}
