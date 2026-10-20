#include "doctest.h"

#include "core/sampler.h"

using namespace nsm;

namespace {
AdapterSample S(const wchar_t* id, uint64_t rx, uint64_t tx, double t) {
    return AdapterSample{id, CounterReading{rx, tx, 1'000'000'000, t}};
}
}

TEST_CASE("Sampler: per-adapter deltas and internet delta") {
    Sampler s; s.Tick({S(L"A",0,0,0), S(L"B",0,0,0)}, L"A", L"A");
    auto r = s.Tick({S(L"A",1000,100,1), S(L"B",500,0,1)}, L"A", L"A");
    CHECK(r.perAdapter.size() == 2); REQUIRE(r.internet); CHECK(r.internet->adapterId == L"A"); CHECK(r.internet->rx == 1000);
    REQUIRE(r.display); CHECK(r.display->rx == doctest::Approx(1000));
}
TEST_CASE("Sampler: internet series follows the internet adapter per tick") {
    Sampler s; s.Tick({S(L"A",0,0,0), S(L"B",0,0,0)}, L"A", L"A");
    auto r1 = s.Tick({S(L"A",1000,0,1), S(L"B",50,0,1)}, L"A", L"A");
    auto r2 = s.Tick({S(L"A",1000,0,2), S(L"B",2050,0,2)}, L"B", L"B");
    REQUIRE(r1.internet); REQUIRE(r2.internet);
    CHECK(r1.internet->adapterId == L"A"); CHECK(r1.internet->rx == 1000);
    CHECK(r2.internet->adapterId == L"B"); CHECK(r2.internet->rx == 2000);
}
TEST_CASE("Sampler: no internet adapter gives no display speed") {
    Sampler s; s.Tick({S(L"A",0,0,0)}, L"", L""); auto r = s.Tick({S(L"A",10,0,1)}, L"", L"");
    CHECK(!r.display); CHECK(!r.internet); CHECK(r.perAdapter.size() == 1);
}
TEST_CASE("Sampler: pinned adapter missing gives no display speed") {
    Sampler s; auto r = s.Tick({S(L"A",0,0,0)}, L"A", L"VPN"); CHECK(!r.display);
}
TEST_CASE("Sampler: invalid speed repeats last valid value, zero if none") {
    Sampler s; auto first = s.Tick({S(L"A",0,0,0)}, L"A", L"A");
    REQUIRE(first.display); CHECK(first.display->rx == 0);
    s.Tick({S(L"A",4000,0,1)}, L"A", L"A");
    auto gap = s.Tick({S(L"A",8000,0,100)}, L"A", L"A");
    REQUIRE(gap.display); CHECK(gap.display->rx == doctest::Approx(4000));
    REQUIRE(gap.internet); CHECK(gap.internet->rx == 4000);
}
TEST_CASE("Sampler: adapter that disappears and returns starts fresh") {
    Sampler s; s.Tick({S(L"A",0,0,0)}, L"A", L"A"); s.Tick({S(L"A",5000,0,1)}, L"A", L"A");
    s.Tick({}, L"", L"A");
    auto back = s.Tick({S(L"A",100,0,3)}, L"A", L"A");
    CHECK(back.perAdapter.empty()); CHECK(!back.internet);
    auto next = s.Tick({S(L"A",1100,0,4)}, L"A", L"A"); REQUIRE(next.internet); CHECK(next.internet->rx == 1000);
}
TEST_CASE("Sampler: adapter that disappears and returns does not reuse its old baseline") {
    Sampler s; s.Tick({S(L"A",0,0,0)}, L"A", L"A"); s.Tick({S(L"A",5000,0,1)}, L"A", L"A");
    s.Tick({}, L"", L"A");
    auto back = s.Tick({S(L"A",6000,0,3)}, L"A", L"A");   // counters >= old: a retained tracker would report 1000
    CHECK(back.perAdapter.empty()); CHECK(!back.internet);
    REQUIRE(back.display); CHECK(back.display->rx == 0);  // stale lastSpeed would be 5000
}
TEST_CASE("Sampler: remembered display speed is kept per adapter") {
    Sampler s; s.Tick({S(L"A",0,0,0), S(L"B",0,0,0)}, L"A", L"A");
    s.Tick({S(L"A",4000,0,1), S(L"B",2000,0,1)}, L"A", L"A");
    auto gapA = s.Tick({S(L"A",8000,0,100), S(L"B",4000,0,100)}, L"A", L"A");   // both adapters gap
    REQUIRE(gapA.display); CHECK(gapA.display->rx == doctest::Approx(4000));
    auto gapB = s.Tick({S(L"A",8000,0,200), S(L"B",4000,0,200)}, L"A", L"B");   // switch display during a gap
    REQUIRE(gapB.display); CHECK(gapB.display->rx == doctest::Approx(2000));      // B's own value, not A's
}
TEST_CASE("Sampler: switching display adapter shows the new adapter's speed") {
    Sampler s; s.Tick({S(L"A",0,0,0), S(L"B",0,0,0)}, L"A", L"A");
    auto r1 = s.Tick({S(L"A",4000,0,1), S(L"B",2000,0,1)}, L"A", L"A");
    auto r2 = s.Tick({S(L"A",8000,0,2), S(L"B",3000,0,2)}, L"A", L"B");
    REQUIRE(r1.display); CHECK(r1.display->rx == doctest::Approx(4000));
    REQUIRE(r2.display); CHECK(r2.display->rx == doctest::Approx(1000));
}
TEST_CASE("Sampler: internet adapter present with a zero delta gives no internet delta") {
    Sampler s; s.Tick({S(L"A",0,0,0), S(L"B",0,0,0)}, L"A", L"A");
    auto r = s.Tick({S(L"A",0,0,1), S(L"B",500,0,1)}, L"A", L"A");
    CHECK(!r.internet);
    REQUIRE(r.perAdapter.size() == 1); CHECK(r.perAdapter[0].adapterId == L"B");
    REQUIRE(r.display); CHECK(r.display->rx == 0);
}
