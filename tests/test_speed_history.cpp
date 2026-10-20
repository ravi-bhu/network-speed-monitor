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

TEST_CASE("SpeedHistory Max floors at 1000") {
    SpeedHistory h;
    h.Push({10, 20});
    CHECK(h.Max() == 1000);
}

TEST_CASE("SpeedHistory partially filled reads back in push order (#4)") {
    SpeedHistory h(5);
    h.Push({1000, 10});
    h.Push({2000, 20});
    h.Push({3000, 30});
    CHECK(h.Size() == 3);
    CHECK(h.Rx() == std::vector<double>{1000, 2000, 3000});
    CHECK(h.Tx() == std::vector<double>{10, 20, 30});
    CHECK(h.Max() == 3000);
}

TEST_CASE("SpeedHistory wraps rx and tx together (#4)") {
    SpeedHistory h(3);
    for (int i = 1; i <= 5; ++i) h.Push({double(i), double(i * 10)});
    CHECK(h.Rx() == std::vector<double>{3, 4, 5});
    CHECK(h.Tx() == std::vector<double>{30, 40, 50});
}

TEST_CASE("SpeedHistory capacity 0 keeps the newest sample (#4)") {
    SpeedHistory h(0);
    CHECK(h.Size() == 0);
    CHECK(h.Rx().empty());
    CHECK(h.Max() == 1000);
    h.Push({5000, 1});
    h.Push({7000, 2});
    CHECK(h.Size() == 1);
    CHECK(h.Rx() == std::vector<double>{7000});
    CHECK(h.Tx() == std::vector<double>{2});
}
