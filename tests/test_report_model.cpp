#include "doctest.h"

#include <windows.h>

#include <ctime>
#include <filesystem>
#include <random>

#include "core/periods.h"
#include "report/report_model.h"

using namespace nsm;
namespace fs = std::filesystem;

namespace {
int64_t UtcOf(int y, int mo, int d, int h, int mi = 0) {
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi;
    return _mkgmtime(&tm);
}
TimeZone Pst() { return *TimeZone::FromKey(L"Pacific Standard Time"); }
int64_t Now() { return UtcOf(2026, 10, 18, 19); }   // Sunday noon PDT

struct TempDb {
    fs::path dir, file;
    TempDb() {
        std::random_device rd;
        dir = fs::temp_directory_path() / (L"nsm_rpt_" + std::to_wstring(rd()) + L"_" + std::to_wstring(rd()));
        file = dir / L"usage.db";
    }
    ~TempDb() { std::error_code ec; fs::remove_all(dir, ec); }
};

void AddHour(UsageStore& st, int64_t h, Series s, const wchar_t* a, const wchar_t* n, uint64_t rx, uint64_t tx) {
    UsageRow r{h, s, a, n, rx, tx};
    st.AddUsage(std::span<const UsageRow>(&r, 1));
}
}  // namespace

TEST_CASE("Summary uses internet series only" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    st.UpsertAdapter(L"vpn", L"VPN", AdapterKind::Virtual);
    st.UpsertAdapter(L"wifi", L"Wi-Fi", AdapterKind::WiFi);
    const auto h = UtcOf(2026, 10, 18, 15);
    AddHour(st, h, Series::Adapter, L"vpn", L"", 600'000'000, 400'000'000);
    AddHour(st, h, Series::Adapter, L"wifi", L"", 650'000'000, 400'000'000);
    AddHour(st, h, Series::Internet, L"vpn", L"", 600'000'000, 400'000'000);
    ReportModel m(st, tz, 0);
    auto s = m.Summary(ResolvePeriod(Period::Today, Now(), tz, 0), Now());
    CHECK(s.today.Total() == 1'000'000'000);
    CHECK(s.today.rx == 600'000'000);
    CHECK(s.period.Total() == 1'000'000'000);
    CHECK(s.thisWeek.Total() == 1'000'000'000);
    CHECK(s.thisMonth.Total() == 1'000'000'000);
}

TEST_CASE("Tables use raw adapter series" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    st.UpsertAdapter(L"vpn", L"VPN", AdapterKind::Virtual);
    st.UpsertAdapter(L"wifi", L"Wi-Fi", AdapterKind::WiFi);
    const auto h = UtcOf(2026, 10, 18, 15);
    AddHour(st, h, Series::Adapter, L"vpn", L"", 600'000'000, 400'000'000);
    AddHour(st, h, Series::Adapter, L"wifi", L"", 650'000'000, 400'000'000);
    AddHour(st, h, Series::Internet, L"vpn", L"", 600'000'000, 400'000'000);
    ReportModel m(st, tz, 0);
    auto rows = m.AdapterTable(ResolvePeriod(Period::Today, Now(), tz, 0), true);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].id == L"wifi"); CHECK(rows[0].totals.Total() == 1'050'000'000);
    CHECK(rows[0].kind == AdapterKind::WiFi);
    CHECK(rows[1].id == L"vpn"); CHECK(rows[1].totals.Total() == 1'000'000'000);
}

TEST_CASE("Busiest day picks the largest internet day in the period" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    AddHour(st, UtcOf(2026, 10, 14, 18), Series::Internet, L"a", L"", 100, 0);
    AddHour(st, UtcOf(2026, 10, 15, 18), Series::Internet, L"a", L"", 300, 0);
    AddHour(st, UtcOf(2026, 10, 15, 20), Series::Internet, L"a", L"", 100, 50);
    AddHour(st, UtcOf(2026, 10, 16, 18), Series::Internet, L"a", L"", 100, 0);
    AddHour(st, UtcOf(2026, 10, 15, 18), Series::Adapter, L"a", L"", 99999, 0);   // raw series ignored
    ReportModel m(st, tz, 0);
    auto s = m.Summary(ResolvePeriod(Period::ThisWeek, Now(), tz, 1), Now());
    REQUIRE(s.busiestDay.has_value());
    CHECK(s.busiestDay->first == LocalDate{2026, 10, 15});
    CHECK(s.busiestDay->second.Total() == 450);
}

TEST_CASE("Busiest day ties go to the earliest date; empty period has none" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    ReportModel m(st, tz, 0);
    CHECK_FALSE(m.Summary(ResolvePeriod(Period::Today, Now(), tz, 0), Now()).busiestDay.has_value());
    AddHour(st, UtcOf(2026, 10, 16, 18), Series::Internet, L"a", L"", 100, 0);
    AddHour(st, UtcOf(2026, 10, 15, 18), Series::Internet, L"a", L"", 100, 0);
    auto s = m.Summary(ResolvePeriod(Period::ThisWeek, Now(), tz, 1), Now());
    REQUIRE(s.busiestDay.has_value());
    CHECK(s.busiestDay->first == LocalDate{2026, 10, 15});
}

TEST_CASE("Friendly name replaces adapter name; hidden excluded unless requested" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    st.UpsertAdapter(L"a", L"Intel Ethernet", AdapterKind::Ethernet);
    st.UpsertAdapter(L"b", L"Loopback", AdapterKind::Other);
    st.SetFriendlyName(L"a", L"Desk cable");
    st.SetHidden(L"b", true);
    const auto h = UtcOf(2026, 10, 18, 15);
    AddHour(st, h, Series::Adapter, L"a", L"", 10, 10);
    AddHour(st, h, Series::Adapter, L"b", L"", 500, 500);
    AddHour(st, h, Series::Adapter, L"zz", L"", 1, 1);   // unknown adapter -> id as name
    ReportModel m(st, tz, 0);
    auto r = ResolvePeriod(Period::Today, Now(), tz, 0);
    auto visible = m.AdapterTable(r, false);
    REQUIRE(visible.size() == 2);
    CHECK(visible[0].name == L"Desk cable");
    CHECK(visible[1].name == L"zz");
    auto all = m.AdapterTable(r, true);
    REQUIRE(all.size() == 3);
    CHECK(all[0].id == L"b"); CHECK(all[0].hidden); CHECK(all[0].name == L"Loopback");
    CHECK(m.Summary(r, Now()).topAdapters.size() == 2);   // hidden left out of the top list too
}

TEST_CASE("Table ties sort by name ascending" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    st.UpsertAdapter(L"1", L"Zeta", AdapterKind::Other);
    st.UpsertAdapter(L"2", L"Alpha", AdapterKind::Other);
    AddHour(st, UtcOf(2026, 10, 18, 15), Series::Adapter, L"1", L"", 5, 5);
    AddHour(st, UtcOf(2026, 10, 18, 15), Series::Adapter, L"2", L"", 5, 5);
    ReportModel m(st, tz, 0);
    auto rows = m.AdapterTable(ResolvePeriod(Period::Today, Now(), tz, 0), false);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].name == L"Alpha");
}

TEST_CASE("WifiTable groups Wi-Fi adapter rows by network and keeps unknown" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    st.UpsertAdapter(L"wifi", L"Wi-Fi", AdapterKind::WiFi);
    st.UpsertAdapter(L"eth", L"Ethernet", AdapterKind::Ethernet);
    st.UpsertNetwork({L"{N1}", L"Home"});
    const auto h = UtcOf(2026, 10, 18, 15);
    AddHour(st, h, Series::Adapter, L"wifi", L"{N1}", 100, 50);
    AddHour(st, h + 3600, Series::Adapter, L"wifi", L"{N1}", 10, 5);
    AddHour(st, h, Series::Adapter, L"wifi", L"", 20, 0);
    AddHour(st, h, Series::Adapter, L"eth", L"{N1}", 9999, 9999);
    AddHour(st, h, Series::Internet, L"wifi", L"{N1}", 7777, 0);
    ReportModel m(st, tz, 0);
    auto rows = m.WifiTable(ResolvePeriod(Period::Today, Now(), tz, 0));
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].name == L"Home"); CHECK(rows[0].totals.rx == 110); CHECK(rows[0].totals.tx == 55);
    CHECK(rows[1].name.empty()); CHECK(rows[1].totals.Total() == 20);
}

TEST_CASE("Hidden Wi-Fi adapters still count in WifiTable" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    st.UpsertAdapter(L"wifi", L"Wi-Fi", AdapterKind::WiFi);
    st.SetHidden(L"wifi", true);
    st.UpsertNetwork({L"{N1}", L"Home"});
    AddHour(st, UtcOf(2026, 10, 18, 15), Series::Adapter, L"wifi", L"{N1}", 1, 1);
    ReportModel m(st, tz, 0);
    CHECK(m.WifiTable(ResolvePeriod(Period::Today, Now(), tz, 0)).size() == 1);
}

TEST_CASE("PickGranularity" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    ReportModel m(st, tz, 0);
    const auto now = Now();
    CHECK(m.PickGranularity(ResolvePeriod(Period::Today, now, tz, 0), now) == Granularity::Hour);
    CHECK(m.PickGranularity(ResolvePeriod(Period::Custom, now, tz, 0, {2026, 7, 8}, {2026, 7, 8}), now) == Granularity::Day);
    CHECK(m.PickGranularity(ResolvePeriod(Period::Last30Days, now, tz, 0), now) == Granularity::Day);
    CHECK(m.PickGranularity(ResolvePeriod(Period::ThisYear, now, tz, 0), now) == Granularity::Month);
    CHECK(m.PickGranularity(ResolvePeriod(Period::AllTime, now, tz, 0), now) == Granularity::Month);
}

TEST_CASE("Timeline on fall-back day has 25 hourly bars" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    const auto now = UtcOf(2026, 11, 1, 20);
    // 2026-11-01 09:00Z = the second 01:00 (PST) hour, index 2 of the day's 25 hours.
    AddHour(st, UtcOf(2026, 11, 1, 9), Series::Internet, L"a", L"", 40, 2);
    ReportModel m(st, tz, 0);
    auto bars = m.Timeline(ResolvePeriod(Period::Today, now, tz, 0), now);
    REQUIRE(bars.size() == 25);
    CHECK(bars[0].start.hour == 0);
    CHECK(bars[1].start.hour == 1);   // 01:00 PDT
    CHECK(bars[2].start.hour == 1);   // 01:00 PST (repeated hour)
    CHECK(bars[24].start.hour == 23);
    CHECK(bars[2].totals.Total() == 42);
    CHECK(bars[1].totals.Total() == 0);
}

TEST_CASE("Timeline daily bars fill gaps with zero bars" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    const auto now = Now();
    AddHour(st, UtcOf(2026, 10, 10, 18), Series::Internet, L"a", L"", 5, 5);
    AddHour(st, UtcOf(2026, 10, 12, 18), Series::Internet, L"a", L"", 7, 0);
    ReportModel m(st, tz, 0);
    auto r = ResolvePeriod(Period::Custom, now, tz, 0, {2026, 10, 9}, {2026, 10, 13});
    auto bars = m.Timeline(r, now);
    REQUIRE(bars.size() == 5);
    CHECK(bars[0].start.day == 9);  CHECK(bars[0].totals.Total() == 0);
    CHECK(bars[1].start.day == 10); CHECK(bars[1].totals.Total() == 10);
    CHECK(bars[2].totals.Total() == 0);
    CHECK(bars[3].totals.Total() == 7);
    CHECK(bars[4].totals.Total() == 0);
}

TEST_CASE("Timeline mixes daily and hourly rows" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    const auto now = Now();
    // Old hourly row (March) is rolled up into a daily row; recent October rows stay hourly.
    AddHour(st, UtcOf(2026, 3, 10, 18), Series::Internet, L"a", L"", 1000, 24);
    st.RollUp(now, tz);
    AddHour(st, UtcOf(2026, 10, 5, 18), Series::Internet, L"a", L"", 300, 0);
    AddHour(st, UtcOf(2026, 10, 6, 18), Series::Internet, L"a", L"", 200, 0);
    REQUIRE(st.QueryDaily({2026, 1, 1}, {2026, 12, 31}).size() == 1);
    ReportModel m(st, tz, 0);
    auto bars = m.Timeline(ResolvePeriod(Period::ThisYear, now, tz, 0), now);
    REQUIRE(bars.size() == 12);   // Jan..Dec of the year
    CHECK(bars[0].start.month == 1); CHECK(bars[0].totals.Total() == 0);
    CHECK(bars[2].start.month == 3); CHECK(bars[2].totals.Total() == 1024);
    CHECK(bars[3].totals.Total() == 0);
    CHECK(bars[9].start.month == 10); CHECK(bars[9].totals.Total() == 500);
    CHECK(bars[9].start.day == 1);
}

TEST_CASE("Timeline for AllTime starts at the first bucket with data" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto tz = Pst();
    const auto now = Now();
    ReportModel m(st, tz, 0);
    auto all = ResolvePeriod(Period::AllTime, now, tz, 0);
    CHECK(m.Timeline(all, now).empty());
    AddHour(st, UtcOf(2026, 8, 5, 18), Series::Internet, L"a", L"", 3, 0);
    auto bars = m.Timeline(all, now);
    REQUIRE(bars.size() == 3);   // Aug, Sep, Oct
    CHECK(bars[0].start.month == 8); CHECK(bars[0].totals.Total() == 3);
}
