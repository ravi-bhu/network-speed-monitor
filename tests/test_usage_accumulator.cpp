#include "doctest.h"

#include <ctime>

#include "core/usage_accumulator.h"

using namespace nsm;

namespace {
int64_t UtcOf(int y, int mo, int d, int h, int mi = 0, int s = 0) {
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s;
    return _mkgmtime(&tm);
}
}

TEST_CASE("Accumulator sums adds in the same hour with the same key") {
    UsageAccumulator a;
    CHECK(a.Empty());
    a.Add(UtcOf(2026,10,18,10,5), Series::Adapter, L"A", L"N", 100, 10);
    a.Add(UtcOf(2026,10,18,10,45), Series::Adapter, L"A", L"N", 50, 5);
    CHECK_FALSE(a.Empty());
    auto rows = a.Take();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].hourUtc == UtcOf(2026,10,18,10));
    CHECK(rows[0].rx == 150); CHECK(rows[0].tx == 15);
}
TEST_CASE("Accumulator splits at the hour boundary") {
    UsageAccumulator a;
    a.Add(UtcOf(2026,10,18,10,59,59), Series::Adapter, L"A", L"N", 1, 1);
    a.Add(UtcOf(2026,10,18,11,0,0), Series::Adapter, L"A", L"N", 1, 1);
    auto rows = a.Take();
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].hourUtc != rows[1].hourUtc);
}
TEST_CASE("Accumulator keeps different series, adapters and networks apart") {
    UsageAccumulator a;
    const auto t = UtcOf(2026,10,18,10);
    a.Add(t, Series::Adapter, L"A", L"N", 1, 1);
    a.Add(t, Series::Internet, L"A", L"N", 1, 1);
    a.Add(t, Series::Adapter, L"B", L"N", 1, 1);
    a.Add(t, Series::Adapter, L"A", L"M", 1, 1);
    CHECK(a.Take().size() == 4);
}
TEST_CASE("Take empties the accumulator") {
    UsageAccumulator a;
    a.Add(UtcOf(2026,10,18,10), Series::Adapter, L"A", L"", 1, 1);
    CHECK(a.Take().size() == 1);
    CHECK(a.Empty());
    CHECK(a.Take().empty());
}
TEST_CASE("ShouldFlush after 300 s or when the hour changes") {
    const auto t = UtcOf(2026,10,18,10,1);
    CHECK_FALSE(ShouldFlush(t, t + 299));
    CHECK(ShouldFlush(t, t + 300));
    CHECK(ShouldFlush(UtcOf(2026,10,18,10) + 3599 - 60, UtcOf(2026,10,18,11)));
}
