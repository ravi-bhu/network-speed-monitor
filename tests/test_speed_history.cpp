#include "doctest.h"

#include "core/speed_history.h"

using namespace nsm;

TEST_CASE("SpeedHistory keeps the newest 60 samples, oldest first") {
    SpeedHistory h;
    for (int i = 0; i < 65; ++i) h.Push({double(i * 1000), 0});
    CHECK(h.Size() == 60);
    CHECK(h.Rx().front() == 5000);
    CHECK(h.Rx().back() == 64000);
    CHECK(h.Max() == 64000);
}

TEST_CASE("SpeedHistory Clear empties it and starts over in order") {
    SpeedHistory h(3);
    for (int i = 0; i < 5; ++i) h.Push({double(i), 0});  // wrapped: head is mid-ring
    h.Clear();
    CHECK(h.Size() == 0);
    CHECK(h.Rx().empty());
    h.Push({7, 0});
    h.Push({8, 0});
    REQUIRE(h.Size() == 2);
    CHECK(h.Rx().front() == 7);
    CHECK(h.Rx().back() == 8);
}

TEST_CASE("SpeedHistory Max floors at 1000") {
    SpeedHistory h;
    h.Push({10, 20});
    CHECK(h.Max() == 1000);
}
