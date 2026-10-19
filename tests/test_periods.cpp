#include "doctest.h"

#include <ctime>

#include "core/periods.h"

using namespace nsm;

namespace {
int64_t UtcOf(int y, int mo, int d, int h, int mi = 0) {
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi;
    return _mkgmtime(&tm);
}
TimeZone Pst() { return *TimeZone::FromKey(L"Pacific Standard Time"); }
TimeZone Zone(const wchar_t* key) {
    auto tz = TimeZone::FromKey(key);
    REQUIRE(tz);
    return *tz;
}
}

TEST_CASE("Today on DST fall-back day is 25 hours" * doctest::test_suite("integration")) {
    auto pst = Pst();
    auto r = ResolvePeriod(Period::Today, UtcOf(2026,11,1,20), pst, 0);
    CHECK(r.begin == UtcOf(2026,11,1,7)); CHECK(r.end == UtcOf(2026,11,2,8));
}
TEST_CASE("Today on DST spring-forward day is 23 hours" * doctest::test_suite("integration")) {
    auto pst = Pst();
    auto r = ResolvePeriod(Period::Today, UtcOf(2026,3,8,20), pst, 0);
    CHECK(r.begin == UtcOf(2026,3,8,8)); CHECK(r.end == UtcOf(2026,3,9,7));
}
TEST_CASE("ThisWeek honours first day of week" * doctest::test_suite("integration")) {   // 2026-10-18 is a Sunday
    auto pst = Pst();
    CHECK(ResolvePeriod(Period::ThisWeek, UtcOf(2026,10,18,19), pst, 1).begin == UtcOf(2026,10,12,7));
    CHECK(ResolvePeriod(Period::ThisWeek, UtcOf(2026,10,18,19), pst, 0).begin == UtcOf(2026,10,18,7));
    CHECK(ResolvePeriod(Period::ThisWeek, UtcOf(2026,10,18,19), pst, 0).end == UtcOf(2026,10,25,7));
}
TEST_CASE("LastWeek is the seven days before ThisWeek" * doctest::test_suite("integration")) {
    auto pst = Pst();
    auto r = ResolvePeriod(Period::LastWeek, UtcOf(2026,10,18,19), pst, 0);
    CHECK(r.begin == UtcOf(2026,10,11,7)); CHECK(r.end == UtcOf(2026,10,18,7));
}
TEST_CASE("Yesterday, months and year" * doctest::test_suite("integration")) {
    auto pst = Pst();
    auto now = UtcOf(2026,1,15,19);
    auto y = ResolvePeriod(Period::Yesterday, now, pst, 0);
    CHECK(y.begin == UtcOf(2026,1,14,8)); CHECK(y.end == UtcOf(2026,1,15,8));
    auto m = ResolvePeriod(Period::ThisMonth, now, pst, 0);
    CHECK(m.begin == UtcOf(2026,1,1,8)); CHECK(m.end == UtcOf(2026,2,1,8));
    auto lm = ResolvePeriod(Period::LastMonth, now, pst, 0);   // December 2025 across the year boundary
    CHECK(lm.begin == UtcOf(2025,12,1,8)); CHECK(lm.end == UtcOf(2026,1,1,8));
    auto yr = ResolvePeriod(Period::ThisYear, now, pst, 0);
    CHECK(yr.begin == UtcOf(2026,1,1,8)); CHECK(yr.end == UtcOf(2027,1,1,8));
}
TEST_CASE("Last30Days is today plus 29 previous days" * doctest::test_suite("integration")) {
    auto pst = Pst();
    auto r = ResolvePeriod(Period::Last30Days, UtcOf(2026,10,18,19), pst, 0);
    CHECK(r.begin == UtcOf(2026,9,19,7)); CHECK(r.end == UtcOf(2026,10,19,7));
}
TEST_CASE("AllTime runs from the epoch to the end of today" * doctest::test_suite("integration")) {
    auto pst = Pst();
    auto r = ResolvePeriod(Period::AllTime, UtcOf(2026,10,18,19), pst, 0);
    CHECK(r.begin == 0); CHECK(r.end == UtcOf(2026,10,19,7));
}
TEST_CASE("Custom range is inclusive of both dates" * doctest::test_suite("integration")) {
    auto pst = Pst();
    auto r = ResolvePeriod(Period::Custom, 0, pst, 0, {2026,1,1}, {2026,1,31});
    CHECK(r.begin == UtcOf(2026,1,1,8)); CHECK(r.end == UtcOf(2026,2,1,8));
}
TEST_CASE("A local day is assigned using the zone, not UTC" * doctest::test_suite("integration")) {
    auto pst = Pst();
    CHECK(LocalDateOf(UtcOf(2026,10,19,6), pst) == LocalDate{2026,10,18});
    CHECK(LocalDateOf(UtcOf(2026,10,19,7), pst) == LocalDate{2026,10,19});
}
TEST_CASE("ToUtc: nonexistent and ambiguous local times give the earliest instant" * doctest::test_suite("integration")) {
    auto pst = Pst();
    // Fall-back: 01:30 occurs twice on 2026-11-01 (08:30Z PDT, 09:30Z PST).
    CHECK(pst.ToUtc({2026,11,1,1,30,0}) == UtcOf(2026,11,1,8,30));
    // Spring-forward: 02:30 on 2026-03-08 does not exist; resolves to the transition instant.
    CHECK(pst.ToUtc({2026,3,8,2,30,0}) == UtcOf(2026,3,8,10));
    // Round trip for an ordinary time.
    auto l = pst.ToLocal(UtcOf(2026,7,4,19));
    CHECK(l.hour == 12); CHECK(l.day == 4);
}
TEST_CASE("HourStart floors to the hour") {
    CHECK(HourStart(UtcOf(2026,5,5,5) + 1799) == UtcOf(2026,5,5,5));
    CHECK(HourStart(UtcOf(1969,12,31,23) + 5) == UtcOf(1969,12,31,23));
}
TEST_CASE("LocalHourStart equals HourStart in a whole-hour zone, DST days included" * doctest::test_suite("integration")) {
    const auto pst = Pst();
    for (int64_t t = UtcOf(2026, 10, 31, 12); t < UtcOf(2026, 11, 2, 12); t += 600) CHECK(LocalHourStart(t, pst) == HourStart(t));
    for (int64_t t = UtcOf(2026, 3, 7, 12); t < UtcOf(2026, 3, 9, 12); t += 600) CHECK(LocalHourStart(t, pst) == HourStart(t));
    // Fall-back: the two 01:00 hours (08:00Z PDT, 09:00Z PST) stay separate buckets.
    CHECK(LocalHourStart(UtcOf(2026, 11, 1, 8, 59), pst) == UtcOf(2026, 11, 1, 8));
    CHECK(LocalHourStart(UtcOf(2026, 11, 1, 9, 0), pst) == UtcOf(2026, 11, 1, 9));
}

TEST_CASE("LocalHourStart floors to the local hour in +5:30 and +5:45" * doctest::test_suite("integration")) {
    const auto ist = Zone(L"India Standard Time");
    // 00:10 IST on Oct 19 is 18:40Z on Oct 18: its bucket is 00:00 IST (18:30Z), the new day.
    CHECK(LocalHourStart(UtcOf(2026, 10, 18, 18, 40), ist) == UtcOf(2026, 10, 18, 18, 30));
    CHECK(LocalHourStart(UtcOf(2026, 10, 18, 18, 30), ist) == UtcOf(2026, 10, 18, 18, 30));
    CHECK(LocalHourStart(UtcOf(2026, 10, 18, 18, 29), ist) == UtcOf(2026, 10, 18, 17, 30));   // 23:59 IST
    const auto npt = Zone(L"Nepal Standard Time");
    // 00:10 NPT on Oct 19 is 18:25Z: bucket 00:00 NPT (18:15Z).
    CHECK(LocalHourStart(UtcOf(2026, 10, 18, 18, 25), npt) == UtcOf(2026, 10, 18, 18, 15));
    CHECK(LocalHourStart(UtcOf(2026, 10, 18, 18, 14), npt) == UtcOf(2026, 10, 18, 17, 15));   // 23:59 NPT
}

TEST_CASE("LocalHourStart across Lord Howe's 30-minute DST" * doctest::test_suite("integration")) {
    const auto lh = Zone(L"Lord Howe Standard Time");
    // Start: 2026-10-04 02:00 LHST (+10:30) -> 02:30 LHDT (+11), at 15:30Z on Oct 3.
    const int64_t on = UtcOf(2026, 10, 3, 15, 30);
    CHECK(lh.ToLocal(on).hour == 2); CHECK(lh.ToLocal(on).minute == 30);
    CHECK(LocalHourStart(on - 600, lh) == on - 3600);         // 01:50 LHST: the 01:00 bucket
    CHECK(LocalHourStart(on, lh) == on);                      // 02:30 LHDT: a short bucket from the transition
    CHECK(LocalHourStart(on + 1799, lh) == on);
    CHECK(LocalHourStart(on + 1800, lh) == on + 1800);        // 03:00 LHDT
    // End: 2026-04-05 02:00 LHDT (+11) -> 01:30 LHST (+10:30), at 15:00Z on Apr 4.
    const int64_t off = UtcOf(2026, 4, 4, 15);
    CHECK(lh.ToLocal(off).hour == 1); CHECK(lh.ToLocal(off).minute == 30);
    CHECK(LocalHourStart(off - 600, lh) == off - 3600);       // 01:50 LHDT
    CHECK(LocalHourStart(off, lh) == off);                    // 01:30 LHST
    CHECK(LocalHourStart(off + 1799, lh) == off);
    CHECK(LocalHourStart(off + 1800, lh) == off + 1800);      // 02:00 LHST
}

TEST_CASE("FirstDayOfWeekFromLocale returns 0..6" * doctest::test_suite("integration")) {
    int d = FirstDayOfWeekFromLocale();
    CHECK(d >= 0); CHECK(d <= 6);
}
TEST_CASE("Midnight inside a DST gap starts the day at the transition" * doctest::test_suite("integration")) {
    // Egypt: on 2026-04-24 clocks jump 00:00 EET (UTC+2) -> 01:00 EEST (UTC+3); local midnight does not exist.
    auto eg = TimeZone::FromKey(L"Egypt Standard Time");
    REQUIRE(eg);
    auto r = ResolvePeriod(Period::Today, UtcOf(2026,4,24,12), *eg, 0);
    CHECK(r.begin == UtcOf(2026,4,23,22));
    CHECK(r.end == UtcOf(2026,4,24,21));   // next midnight is EEST: 23 hours
}
