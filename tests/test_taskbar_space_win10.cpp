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

TEST_CASE("ReapplyLimiter allows a burst, then backs off, then allows again") {
    ReapplyLimiter limiter(3, 2000, 10000);
    CHECK(limiter.Allow(1000));
    CHECK(limiter.Allow(1100));
    CHECK(limiter.Allow(1200));
    CHECK(!limiter.Allow(1300));    // 4th within 2 s: back off until 11300
    CHECK(!limiter.Allow(5000));
    CHECK(!limiter.Allow(11299));
    CHECK(limiter.Allow(11300));    // back-off over, history cleared
    CHECK(limiter.Allow(11400));
}

TEST_CASE("ReapplyLimiter forgets applies older than its window") {
    ReapplyLimiter limiter(3, 2000, 10000);
    for (uint64_t t = 0; t < 60000; t += 700) CHECK(limiter.Allow(t));  // never 4 within 2 s
}

namespace {
const Rect kNatural{2, 0, 1473, 50};   // task list in rebar client coordinates
const Rect kOverlay{1307, 5, 1466, 45};
const Rect kShrunk{2, 0, 1302, 50};    // ends 5 px before the overlay
}  // namespace

TEST_CASE("TaskListReservation: shrink on reserve, nothing while stable, restore to natural") {
    TaskListReservation r;
    CHECK(!r.SetOverlay(kOverlay, Edge::Bottom, 5, kNatural, 0));  // not attached
    r.Attach(kNatural);
    CHECK(r.Attached());
    CHECK(r.SetOverlay(kOverlay, Edge::Bottom, 5, kNatural, 0) == kShrunk);
    CHECK(!r.SetOverlay(kOverlay, Edge::Bottom, 5, kShrunk, 10));   // same request
    CHECK(!r.Observe(kShrunk, 20));                                   // our own resize echoing back
    CHECK(r.RestoreTarget(kShrunk) == kNatural);
    r.Detach();
    CHECK(!r.Attached());
    CHECK(!r.RestoreTarget(kShrunk));
}

TEST_CASE("TaskListReservation: Explorer's new layout is adopted and re-shrunk") {
    TaskListReservation r;
    r.Attach(kNatural);
    REQUIRE(r.SetOverlay(kOverlay, Edge::Bottom, 5, kNatural, 0) == kShrunk);
    const Rect relaid{2, 0, 1440, 50};  // the tray grew; Explorer reset the band
    CHECK(r.Observe(relaid, 100) == kShrunk);
    CHECK(r.RestoreTarget(kShrunk) == relaid);  // the adopted natural size
    // A new layout that already clears the overlay needs no write, and nothing to restore.
    const Rect narrow{2, 0, 1200, 50};
    CHECK(!r.Observe(narrow, 200));
    CHECK(!r.RestoreTarget(narrow));
}

TEST_CASE("TaskListReservation: restore leaves Explorer's layout alone once it changed it") {
    TaskListReservation r;
    r.Attach(kNatural);
    REQUIRE(r.SetOverlay(kOverlay, Edge::Bottom, 5, kNatural, 0) == kShrunk);
    // e.g. WM_DISPLAYCHANGE: Explorer re-laid out for the new resolution before we restored.
    CHECK(!r.RestoreTarget({2, 0, 1100, 40}));
    CHECK(!r.RestoreTarget(kNatural));  // differs from what we set: Explorer's, not ours
}

TEST_CASE("TaskListReservation: an empty overlay asks for the natural size") {
    TaskListReservation r;
    r.Attach(kNatural);
    REQUIRE(r.SetOverlay(kOverlay, Edge::Bottom, 5, kNatural, 0) == kShrunk);
    CHECK(r.SetOverlay({}, Edge::Bottom, 0, kShrunk, 10) == kNatural);
    CHECK(!r.RestoreTarget(kNatural));
}

TEST_CASE("TaskListReservation: a tug-of-war with Explorer is damped, then retried") {
    TaskListReservation r(ReapplyLimiter(3, 2000, 10000));
    r.Attach(kNatural);
    REQUIRE(r.SetOverlay(kOverlay, Edge::Bottom, 5, kNatural, 0) == kShrunk);  // apply 1
    CHECK(r.Observe(kNatural, 100) == kShrunk);                                // Explorer undoes it: apply 2
    CHECK(r.Observe(kNatural, 200) == kShrunk);                                // apply 3
    CHECK(!r.Observe(kNatural, 300));                                          // 4th: backing off
    CHECK(!r.Observe(kNatural, 5000));
    CHECK(!r.RestoreTarget(kNatural));  // Explorer owns the layout meanwhile
    CHECK(r.Observe(kNatural, 10300) == kShrunk);  // periodic verify after the back-off
}

TEST_CASE("TaskListReservation: a denied overlay change is applied by a later verify") {
    TaskListReservation r(ReapplyLimiter(1, 2000, 1000));
    r.Attach(kNatural);
    REQUIRE(r.SetOverlay(kOverlay, Edge::Bottom, 5, kNatural, 0) == kShrunk);
    const Rect wider{1250, 5, 1466, 45};
    CHECK(!r.SetOverlay(wider, Edge::Bottom, 5, kShrunk, 10));  // limited
    CHECK(!r.Observe(kShrunk, 500));                             // still backing off
    CHECK(r.Observe(kShrunk, 1100) == Rect{2, 0, 1245, 50});    // retried though nothing moved
}

TEST_CASE("TaskListReservation: a write skipped for a hung Explorer is owed, not mistaken for a new layout (#8)") {
    TaskListReservation r;
    r.Attach(kNatural);
    REQUIRE(r.SetOverlay(kOverlay, Edge::Bottom, 5, kNatural, 0) == kShrunk);
    r.Unapplied(kNatural);                        // Explorer was hung: nothing written
    CHECK(!r.RestoreTarget(kNatural));            // nothing of ours to undo
    CHECK(r.Observe(kNatural, 100) == kShrunk);   // retried; kNatural stays the natural size
    CHECK(r.RestoreTarget(kShrunk) == kNatural);
    // A skipped shrink after an earlier one landed: the task list keeps our first shrink.
    const Rect wider{1250, 5, 1466, 45};
    REQUIRE(r.SetOverlay(wider, Edge::Bottom, 5, kShrunk, 200) == Rect{2, 0, 1245, 50});
    r.Unapplied(kShrunk);
    CHECK(r.RestoreTarget(kShrunk) == kNatural);  // still ours to restore
    CHECK(r.Observe(kShrunk, 300) == Rect{2, 0, 1245, 50});
}
