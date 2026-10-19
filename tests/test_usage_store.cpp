#include "doctest.h"

#include <windows.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>

#include "storage/usage_store.h"

using namespace nsm;
namespace fs = std::filesystem;

namespace {
int64_t UtcOf(int y, int mo, int d, int h, int mi = 0) {
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi;
    return _mkgmtime(&tm);
}

// Unique temp directory, removed (with -wal/-shm and recovered files) on destruction.
struct TempDb {
    fs::path dir;
    fs::path file;
    TempDb() {
        std::random_device rd;
        dir = fs::temp_directory_path() / (L"nsm_test_" + std::to_wstring(rd()) + L"_" + std::to_wstring(rd()));
        file = dir / L"usage.db";
    }
    ~TempDb() { std::error_code ec; fs::remove_all(dir, ec); }
};

UsageRow Row(int64_t h, Series s, const wchar_t* a, const wchar_t* n, uint64_t rx, uint64_t tx) {
    return UsageRow{h, s, a, n, rx, tx};
}

constexpr int64_t kFarFuture = 4000000000;
size_t HourlyCount(UsageStore& st) { return st.QueryHourly({0, kFarFuture}).size(); }
size_t DailyCount(UsageStore& st) { return st.QueryDaily({1970, 1, 1}, {2999, 12, 31}).size(); }
}

TEST_CASE("Open creates the parent directory and an empty db") {
    TempDb t;
    UsageStore st;
    CHECK(st.Open(t.file) == UsageStore::OpenResult::Ok);
    CHECK(fs::exists(t.file));
    CHECK(HourlyCount(st) == 0);
}

TEST_CASE("AddUsage adds to existing rows") {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    const auto h = UtcOf(2026, 10, 18, 10);
    UsageRow r = Row(h, Series::Adapter, L"A", L"net", 100, 20);
    st.AddUsage(std::span<const UsageRow>(&r, 1));
    st.AddUsage(std::span<const UsageRow>(&r, 1));
    auto rows = st.QueryHourly({h, h + 3600});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].rx == 200); CHECK(rows[0].tx == 40);
    CHECK(rows[0].adapterId == L"A"); CHECK(rows[0].networkKey == L"net");
    CHECK(rows[0].series == Series::Adapter);
}

TEST_CASE("Unicode strings and large counters round-trip") {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    const auto h = UtcOf(2026, 10, 18, 10);
    UsageRow r = Row(h, Series::Internet, L"{GUID-é}", L"ネット", 5000000000000ull, 1);
    st.AddUsage(std::span<const UsageRow>(&r, 1));
    auto rows = st.QueryHourly({h, h + 1});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].adapterId == r.adapterId); CHECK(rows[0].networkKey == r.networkKey);
    CHECK(rows[0].rx == 5000000000000ull);
    CHECK(rows[0].series == Series::Internet);
}

TEST_CASE("QueryHourly is half-open") {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    const auto b = UtcOf(2026, 10, 18, 10), e = UtcOf(2026, 10, 18, 11);
    UsageRow rows[] = { Row(b, Series::Adapter, L"A", L"", 1, 1), Row(e, Series::Adapter, L"A", L"", 2, 2) };
    st.AddUsage(rows);
    auto got = st.QueryHourly({b, e});
    REQUIRE(got.size() == 1);
    CHECK(got[0].hourUtc == b);
}

TEST_CASE("RollUp moves rows older than 90 days to local-day rows" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto pst = *TimeZone::FromKey(L"Pacific Standard Time");
    const auto now = UtcOf(2026, 10, 18, 12);
    UsageRow rows[] = {
        Row(UtcOf(2026, 7, 1, 6), Series::Adapter, L"A", L"", 10, 1),
        Row(UtcOf(2026, 7, 1, 8), Series::Adapter, L"A", L"", 20, 2),
        Row(UtcOf(2026, 10, 10, 0), Series::Adapter, L"A", L"", 30, 3),
    };
    st.AddUsage(rows);
    st.RollUp(now, pst);
    auto d = st.QueryDaily({2026, 6, 30}, {2026, 7, 1});
    REQUIRE(d.size() == 2);
    CHECK(d[0].day == LocalDate{2026, 6, 30}); CHECK(d[0].rx == 10);
    CHECK(d[1].day == LocalDate{2026, 7, 1});  CHECK(d[1].rx == 20);
    auto h = st.QueryHourly({0, now});
    REQUIRE(h.size() == 1);
    CHECK(h[0].hourUtc == UtcOf(2026, 10, 10, 0));
    st.RollUp(now, pst);
    CHECK(DailyCount(st) == 2);
    CHECK(HourlyCount(st) == 1);
}

TEST_CASE("RollUp adds to an existing daily row" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto pst = *TimeZone::FromKey(L"Pacific Standard Time");
    UsageRow rows[] = {
        Row(UtcOf(2026, 7, 1, 10), Series::Adapter, L"A", L"", 10, 1),
        Row(UtcOf(2026, 7, 1, 11), Series::Adapter, L"A", L"", 20, 2),
    };
    st.AddUsage(rows);
    st.RollUp(UtcOf(2026, 10, 18, 12), pst);
    UsageRow more = Row(UtcOf(2026, 7, 1, 12), Series::Adapter, L"A", L"", 5, 5);
    st.AddUsage(std::span<const UsageRow>(&more, 1));
    st.RollUp(UtcOf(2026, 10, 18, 12), pst);
    auto d = st.QueryDaily({2026, 7, 1}, {2026, 7, 1});
    REQUIRE(d.size() == 1);
    CHECK(d[0].rx == 35); CHECK(d[0].tx == 8);
}

TEST_CASE("Clears remove only what they target" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto pst = *TimeZone::FromKey(L"Pacific Standard Time");
    const auto now = UtcOf(2026, 10, 18, 12);
    st.UpsertAdapter(L"A", L"Adapter A", AdapterKind::WiFi);
    st.UpsertAdapter(L"B", L"Adapter B", AdapterKind::Ethernet);
    st.UpsertNetwork({L"n1", L"Home"});
    st.UpsertNetwork({L"n2", L"Work"});
    auto fill = [&] {
        UsageRow rows[] = {
            Row(UtcOf(2026, 7, 1, 6), Series::Adapter, L"A", L"n1", 1, 1),
            Row(UtcOf(2026, 7, 1, 6), Series::Adapter, L"B", L"n2", 1, 1),
            Row(UtcOf(2026, 10, 17, 6), Series::Adapter, L"A", L"n1", 1, 1),
            Row(UtcOf(2026, 10, 17, 6), Series::Adapter, L"B", L"n2", 1, 1),
        };
        st.AddUsage(rows);
        st.RollUp(now, pst);
    };
    fill();
    REQUIRE(HourlyCount(st) == 2); REQUIRE(DailyCount(st) == 2);

    st.ClearAdapter(L"A");
    CHECK(HourlyCount(st) == 1); CHECK(DailyCount(st) == 1);
    CHECK(st.QueryHourly({0, kFarFuture})[0].adapterId == L"B");
    CHECK(st.QueryDaily({1970, 1, 1}, {2999, 1, 1})[0].adapterId == L"B");

    st.ClearAll();
    fill();
    st.ClearNetwork(L"n2");
    CHECK(HourlyCount(st) == 1); CHECK(DailyCount(st) == 1);
    CHECK(st.QueryHourly({0, kFarFuture})[0].networkKey == L"n1");

    st.ClearAll();
    CHECK(HourlyCount(st) == 0); CHECK(DailyCount(st) == 0);
    CHECK(st.Adapters().size() == 2);
    CHECK(st.Networks().size() == 2);
}

TEST_CASE("UpsertAdapter preserves friendly name and hidden") {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    st.UpsertAdapter(L"A", L"Wi-Fi", AdapterKind::WiFi);
    st.SetFriendlyName(L"A", L"Living room");
    st.SetHidden(L"A", true);
    st.UpsertAdapter(L"A", L"Wi-Fi 2", AdapterKind::Ethernet);
    auto a = st.Adapters();
    REQUIRE(a.size() == 1);
    CHECK(a[0].name == L"Wi-Fi 2"); CHECK(a[0].kind == AdapterKind::Ethernet);
    CHECK(a[0].friendlyName == L"Living room"); CHECK(a[0].hidden);
    st.SetFriendlyName(L"A", L"");
    st.SetHidden(L"A", false);
    a = st.Adapters();
    CHECK(a[0].friendlyName.empty()); CHECK_FALSE(a[0].hidden);
}

TEST_CASE("UpsertNetwork updates the name") {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    st.UpsertNetwork({L"k", L"Old"});
    st.UpsertNetwork({L"k", L"New"});
    auto n = st.Networks();
    REQUIRE(n.size() == 1);
    CHECK(n[0].name == L"New");
}

TEST_CASE("Data persists across reopen") {
    TempDb t;
    const auto h = UtcOf(2026, 10, 18, 10);
    {
        UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
        UsageRow r = Row(h, Series::Adapter, L"A", L"", 7, 8);
        st.AddUsage(std::span<const UsageRow>(&r, 1));
    }
    UsageStore st2;
    CHECK(st2.Open(t.file) == UsageStore::OpenResult::Ok);
    auto rows = st2.QueryHourly({h, h + 1});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].rx == 7);
}

TEST_CASE("Open recovers from a corrupt file") {
    TempDb t;
    fs::create_directories(t.dir);
    {
        std::ofstream f(t.file, std::ios::binary);
        std::string junk(4096, static_cast<char>(0xAB));
        f.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    }
    UsageStore st;
    CHECK(st.Open(t.file) == UsageStore::OpenResult::RecoveredFromCorruption);
    CHECK(fs::exists(st.RecoveredFile()));
    CHECK(st.RecoveredFile().parent_path() == t.dir);
    CHECK(st.RecoveredFile().filename().wstring().rfind(L"usage.corrupt-", 0) == 0);
    CHECK(st.RecoveredFile().extension() == L".db");
    UsageRow r = Row(UtcOf(2026, 10, 18, 10), Series::Adapter, L"A", L"", 1, 1);
    st.AddUsage(std::span<const UsageRow>(&r, 1));
    CHECK(HourlyCount(st) == 1);
}

TEST_CASE("Open reports Failed when the path cannot be used") {
    TempDb t;
    fs::create_directories(t.file);   // a directory where the db file should be
    UsageStore st;
    CHECK(st.Open(t.file) == UsageStore::OpenResult::Failed);
    UsageRow r = Row(UtcOf(2026, 10, 18, 10), Series::Adapter, L"A", L"", 1, 1);
    st.AddUsage(std::span<const UsageRow>(&r, 1));   // must not throw
    CHECK(st.QueryHourly({0, kFarFuture}).empty());
}
