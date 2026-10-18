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
