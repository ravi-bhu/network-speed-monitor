#include "doctest.h"

#include "core/counter_tracker.h"

using namespace nsm;

namespace {
CounterReading R(uint64_t rx, uint64_t tx, uint64_t link, double t) { return CounterReading{rx, tx, link, t}; }
}

TEST_CASE("CounterTracker: first reading is a baseline only") {
    CounterTracker t; auto r = t.Update(R(500, 500, 1'000'000'000, 0.0));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 0); CHECK(r.txDelta == 0);
}
TEST_CASE("CounterTracker: normal second gives speed and deltas") {
    CounterTracker t; t.Update(R(0, 0, 1'000'000'000, 10.0));
    auto r = t.Update(R(1'000'000, 500'000, 1'000'000'000, 10.5));
    CHECK(r.speedValid); CHECK(r.speed.rx == doctest::Approx(2'000'000)); CHECK(r.speed.tx == doctest::Approx(1'000'000));
    CHECK(r.rxDelta == 1'000'000); CHECK(r.txDelta == 500'000);
}
TEST_CASE("CounterTracker: counter going backwards resets baseline") {
    CounterTracker t; t.Update(R(9'000, 9'000, 1'000'000'000, 0)); auto r = t.Update(R(100, 100, 1'000'000'000, 1));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 0);
    auto n = t.Update(R(1'100, 600, 1'000'000'000, 2)); CHECK(n.speedValid); CHECK(n.rxDelta == 1'000); CHECK(n.txDelta == 500);
}
TEST_CASE("CounterTracker: TX-only counter going backwards resets baseline") {
    CounterTracker t; t.Update(R(1'000, 9'000, 1'000'000'000, 0)); auto r = t.Update(R(2'000, 100, 1'000'000'000, 1));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 0); CHECK(r.txDelta == 0);
    auto n = t.Update(R(3'000, 600, 1'000'000'000, 2)); CHECK(n.speedValid); CHECK(n.rxDelta == 1'000); CHECK(n.txDelta == 500);
}
TEST_CASE("CounterTracker: implausible delta beyond link speed is dropped") {
    CounterTracker t; t.Update(R(0, 0, 100'000'000, 0));                // 100 Mbps = 12.5 MB/s
    auto r = t.Update(R(50'000'000, 0, 100'000'000, 1));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 0);
    auto n = t.Update(R(51'000'000, 0, 100'000'000, 2)); CHECK(n.rxDelta == 1'000'000);  // baseline moved
}
TEST_CASE("CounterTracker: implausible TX-only delta is dropped") {
    CounterTracker t; t.Update(R(0, 0, 100'000'000, 0));
    auto r = t.Update(R(1'000, 50'000'000, 100'000'000, 1));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 0); CHECK(r.txDelta == 0);
    auto n = t.Update(R(2'000, 51'000'000, 100'000'000, 2)); CHECK(n.rxDelta == 1'000); CHECK(n.txDelta == 1'000'000);
}
TEST_CASE("CounterTracker: gap of exactly kGapSeconds still gives speed") {
    CounterTracker t; t.Update(R(0, 0, 1'000'000'000, 10.0));
    auto r = t.Update(R(3'000'000, 1'500'000, 1'000'000'000, 10.0 + kGapSeconds));
    CHECK(r.speedValid); CHECK(r.speed.rx == doctest::Approx(1'000'000)); CHECK(r.speed.tx == doctest::Approx(500'000));
    CHECK(r.rxDelta == 3'000'000); CHECK(r.txDelta == 1'500'000);
}
TEST_CASE("CounterTracker: long gap counts usage but not speed") {
    CounterTracker t; t.Update(R(0, 0, 1'000'000'000, 0));
    auto r = t.Update(R(2'000'000'000, 30'000'000, 1'000'000'000, 3600));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 2'000'000'000); CHECK(r.txDelta == 30'000'000);
}
TEST_CASE("CounterTracker: unknown link speed uses 100 Gbps cap") {
    CounterTracker t; t.Update(R(0, 0, 0, 0));
    CHECK(t.Update(R(5'000'000'000, 0, 0, 1)).rxDelta == 5'000'000'000);
}
TEST_CASE("CounterTracker: unknown link speed drops a delta beyond the 100 Gbps cap") {
    CounterTracker t; t.Update(R(0, 0, 0, 0));                          // 100 Gbps = 12.5 GB/s, 13.75 GB with margin
    auto r = t.Update(R(14'000'000'000, 0, 0, 1));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 0);
    CHECK(t.Update(R(15'000'000'000, 0, 0, 2)).rxDelta == 1'000'000'000);   // baseline moved
}
TEST_CASE("CounterTracker: non-increasing time is ignored") {
    CounterTracker t; t.Update(R(0, 0, 1'000'000'000, 5)); auto r = t.Update(R(1'000, 0, 1'000'000'000, 5));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 0);
    CHECK(t.Update(R(2'000, 0, 1'000'000'000, 6)).rxDelta == 2'000);   // baseline was not moved
}
