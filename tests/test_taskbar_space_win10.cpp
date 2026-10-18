#include "doctest.h"

#include "ui/taskbar_space_win10.h"

using namespace nsm;

TEST_CASE("ReservedTaskListRect ends the task list a margin before the overlay (horizontal)") {
    const Rect natural{180, 1030, 1649, 1080};
    const Rect overlay{1479, 1035, 1644, 1075};
    CHECK(ReservedTaskListRect(natural, overlay, Edge::Bottom, 4) == Rect{180, 1030, 1475, 1080});
    const Rect top{180, 0, 1649, 50};
    CHECK(ReservedTaskListRect(top, {1479, 5, 1644, 45}, Edge::Top, 4) == Rect{180, 0, 1475, 50});
}

TEST_CASE("ReservedTaskListRect ends the task list a margin above the overlay (vertical)") {
    const Rect natural{0, 200, 62, 950};
    CHECK(ReservedTaskListRect(natural, {4, 900, 58, 940}, Edge::Left, 4) == Rect{0, 200, 62, 896});
    const Rect right{1858, 200, 1920, 950};
    CHECK(ReservedTaskListRect(right, {1862, 868, 1916, 940}, Edge::Right, 6) == Rect{1858, 200, 1920, 862});
}

TEST_CASE("ReservedTaskListRect leaves the task list alone when it already clears the overlay") {
    const Rect natural{180, 1030, 1400, 1080};
    CHECK(ReservedTaskListRect(natural, {1479, 1035, 1644, 1075}, Edge::Bottom, 4) == natural);
    CHECK(ReservedTaskListRect(natural, {1404, 1035, 1500, 1075}, Edge::Bottom, 4) == natural);
}

TEST_CASE("ReservedTaskListRect with no overlay is the natural rect") {
    const Rect natural{180, 1030, 1649, 1080};
    CHECK(ReservedTaskListRect(natural, {}, Edge::Bottom, 4) == natural);
    CHECK(ReservedTaskListRect({0, 200, 62, 950}, {}, Edge::Left, 4) == Rect{0, 200, 62, 950});
}

TEST_CASE("ReservedTaskListRect never inverts the task list") {
    const Rect natural{180, 1030, 600, 1080};
    CHECK(ReservedTaskListRect(natural, {100, 1035, 400, 1075}, Edge::Bottom, 4) == Rect{180, 1030, 180, 1080});
    CHECK(ReservedTaskListRect({0, 200, 62, 400}, {4, 150, 58, 300}, Edge::Left, 4) == Rect{0, 200, 62, 200});
}
