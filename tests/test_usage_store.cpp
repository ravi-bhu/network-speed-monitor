#include "doctest.h"

#include <windows.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "sqlite3.h"
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

void WriteFile(const fs::path& p, const std::string& bytes) {
    std::ofstream f(p, std::ios::binary);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string ReadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

bool HasBytes(const fs::path& p, const std::string& bytes) { return ReadFile(p) == bytes; }

// A real WAL-mode database whose table root (page 2) is overwritten: the header is valid, so the file opens and
// only PRAGMA quick_check finds the damage.
void WriteDamagedDb(const fs::path& file) {
    fs::create_directories(file.parent_path());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(file.string().c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db,
        "PRAGMA page_size=4096; PRAGMA journal_mode=WAL; CREATE TABLE t(x);"
        "WITH RECURSIVE c(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM c WHERE i < 200)"
        " INSERT INTO t SELECT randomblob(100) FROM c;", nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_close(db) == SQLITE_OK);
    std::fstream f(file, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(4096);
    const std::string junk(4096, static_cast<char>(0xAB));
    f.write(junk.data(), static_cast<std::streamsize>(junk.size()));
}

// An open handle on a file, with the given sharing. Without FILE_SHARE_DELETE the file can't be renamed.
struct HeldFile {
    HANDLE h;
    HeldFile(const fs::path& p, DWORD share)
        : h(CreateFileW(p.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
    ~HeldFile() { Release(); }
    HeldFile(const HeldFile&) = delete;
    HeldFile& operator=(const HeldFile&) = delete;
    bool ok() const { return h != INVALID_HANDLE_VALUE; }
    void Release() { if (ok()) CloseHandle(h); h = INVALID_HANDLE_VALUE; }
};

// Holds a shared lock on SQLite's shared-lock byte range of a database file, as a reader in another process
// would. SQLite's last connection then can't take the exclusive lock it needs to checkpoint and delete the -wal
// and -shm when it closes, so they stay on disk. The handle allows rename, so the file can still be moved aside.
struct SharedReaderLock {
    static constexpr DWORD kSharedFirst = 0x40000000 + 2, kSharedSize = 510;   // PENDING_BYTE + 2, SHARED_SIZE
    HeldFile f;
    bool locked = false;
    explicit SharedReaderLock(const fs::path& db) : f(db, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE) {
        OVERLAPPED ov{};
        ov.Offset = kSharedFirst;
        locked = f.ok() && LockFileEx(f.h, LOCKFILE_FAIL_IMMEDIATELY, 0, kSharedSize, 0, &ov);
    }
    ~SharedReaderLock() {
        OVERLAPPED ov{};
        ov.Offset = kSharedFirst;
        if (locked) UnlockFileEx(f.h, 0, kSharedSize, 0, &ov);
    }
    SharedReaderLock(const SharedReaderLock&) = delete;
    SharedReaderLock& operator=(const SharedReaderLock&) = delete;
};

// The moved-aside databases in dir (not their -wal and -shm).
std::vector<fs::path> CorruptCopies(const fs::path& dir) {
    std::vector<fs::path> out;
    for (const auto& e : fs::directory_iterator(dir)) {
        const fs::path& p = e.path();
        if (p.filename().wstring().rfind(L"usage.corrupt-", 0) == 0 && p.extension() == L".db") out.push_back(p);
    }
    return out;
}
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

    // A second RollUp at the same time changes nothing: same rows with the same values.
    st.RollUp(now, pst);
    auto d2 = st.QueryDaily({1970, 1, 1}, {2999, 12, 31});
    REQUIRE(d2.size() == 2);
    for (size_t i = 0; i < d2.size(); ++i) {
        CHECK(d2[i].day == d[i].day); CHECK(d2[i].adapterId == d[i].adapterId);
        CHECK(d2[i].rx == d[i].rx); CHECK(d2[i].tx == d[i].tx);
    }
    CHECK(d2[0].tx == 1); CHECK(d2[1].tx == 2);
    auto h2 = st.QueryHourly({0, kFarFuture});
    REQUIRE(h2.size() == 1);
    CHECK(h2[0].hourUtc == h[0].hourUtc); CHECK(h2[0].rx == 30); CHECK(h2[0].tx == 3);
    CHECK(st.Healthy());
}

TEST_CASE("RollUp keeps the row at the cutoff and rolls up the hour before it" * doctest::test_suite("integration")) {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    auto pst = *TimeZone::FromKey(L"Pacific Standard Time");
    const auto now = UtcOf(2026, 10, 18, 12, 30);
    const auto cutoff = UtcOf(2026, 10, 18, 12) - 90LL * 86400;   // PDT on both dates, so exactly 90 days of hours
    REQUIRE(cutoff == UtcOf(2026, 7, 20, 12));
    UsageRow rows[] = {
        Row(cutoff - 3600, Series::Adapter, L"A", L"", 10, 1),       // 04:00 PDT on 20 July: rolled up
        Row(cutoff, Series::Adapter, L"A", L"", 20, 2),              // exactly at the cutoff: stays hourly
    };
    REQUIRE(st.AddUsage(rows));
    st.RollUp(now, pst);
    auto d = st.QueryDaily({1970, 1, 1}, {2999, 12, 31});
    REQUIRE(d.size() == 1);
    CHECK(d[0].day == LocalDate{2026, 7, 20}); CHECK(d[0].rx == 10); CHECK(d[0].tx == 1);
    auto h = st.QueryHourly({0, kFarFuture});
    REQUIRE(h.size() == 1);
    CHECK(h[0].hourUtc == cutoff); CHECK(h[0].rx == 20); CHECK(h[0].tx == 2);

    st.RollUp(now + 3600, pst);                                      // an hour later the cutoff row goes too
    d = st.QueryDaily({1970, 1, 1}, {2999, 12, 31});
    REQUIRE(d.size() == 1);
    CHECK(d[0].rx == 30); CHECK(d[0].tx == 3);
    CHECK(HourlyCount(st) == 0);
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
    WriteFile(t.file, std::string(4096, static_cast<char>(0xAB)));
    const std::string wal(4096, static_cast<char>(0xCD));
    WriteFile(t.file.wstring() + L"-wal", wal);
    WriteFile(t.file.wstring() + L"-shm", std::string(32768, static_cast<char>(0xEF)));
    UsageStore st;
    {
        SharedReaderLock reader(t.file);   // else SQLite deletes the -wal and -shm when it closes the file
        REQUIRE(reader.locked);
        CHECK(st.Open(t.file) == UsageStore::OpenResult::RecoveredFromCorruption);
    }
    CHECK(fs::exists(st.RecoveredFile()));
    CHECK(st.RecoveredFile().parent_path() == t.dir);
    CHECK(st.RecoveredFile().filename().wstring().rfind(L"usage.corrupt-", 0) == 0);
    CHECK(st.RecoveredFile().extension() == L".db");
    CHECK(HasBytes(st.RecoveredFile().wstring() + L"-wal", wal));     // moved with the database
    CHECK(fs::exists(st.RecoveredFile().wstring() + L"-shm"));        // (SQLite rewrites its content)
    CHECK_FALSE(HasBytes(t.file.wstring() + L"-wal", wal));           // not left for the fresh database
    UsageRow r = Row(UtcOf(2026, 10, 18, 10), Series::Adapter, L"A", L"", 1, 1);
    st.AddUsage(std::span<const UsageRow>(&r, 1));
    CHECK(HourlyCount(st) == 1);
}

TEST_CASE("Open recovers a valid-header file that fails quick_check, moving its -wal and -shm") {
    TempDb t;
    WriteDamagedDb(t.file);
    const std::string wal(4096, static_cast<char>(0xCD));
    WriteFile(t.file.wstring() + L"-wal", wal);
    UsageStore st;
    {
        SharedReaderLock reader(t.file);
        REQUIRE(reader.locked);
        CHECK(st.Open(t.file) == UsageStore::OpenResult::RecoveredFromCorruption);
    }
    const fs::path moved = st.RecoveredFile();
    CHECK(CorruptCopies(t.dir) == std::vector<fs::path>{moved});
    CHECK(HasBytes(moved.wstring() + L"-wal", wal));
    CHECK(fs::exists(moved.wstring() + L"-shm"));
    CHECK_FALSE(HasBytes(t.file.wstring() + L"-wal", wal));
    CHECK(st.Healthy());
    CHECK(HourlyCount(st) == 0);
    UsageRow r = Row(UtcOf(2026, 10, 18, 10), Series::Adapter, L"A", L"", 1, 1);
    CHECK(st.AddUsage(std::span<const UsageRow>(&r, 1)));
}

TEST_CASE("Open fails and leaves the corrupt file in place when its -wal can't be moved") {
    TempDb t;
    WriteDamagedDb(t.file);
    const std::string damaged = ReadFile(t.file);
    const fs::path walPath = t.file.wstring() + L"-wal";
    const std::string wal(4096, static_cast<char>(0xCD));
    WriteFile(walPath, wal);
    UsageStore st;
    {
        SharedReaderLock reader(t.file);
        REQUIRE(reader.locked);
        HeldFile heldWal(walPath, FILE_SHARE_READ | FILE_SHARE_WRITE);   // no FILE_SHARE_DELETE: can't be moved
        REQUIRE(heldWal.ok());
        CHECK(st.Open(t.file) == UsageStore::OpenResult::Failed);
        CHECK_FALSE(st.Healthy());
        CHECK(st.RecoveredFile().empty());
    }
    CHECK(CorruptCopies(t.dir).empty());
    CHECK(HasBytes(t.file, damaged));                                 // the database was put back
    CHECK(HasBytes(walPath, wal));

    CHECK(st.Open(t.file) == UsageStore::OpenResult::RecoveredFromCorruption);   // once the -wal is free
    CHECK(CorruptCopies(t.dir).size() == 1);
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

TEST_CASE("ClassifyQuickCheck: only corruption moves the database aside") {
    CHECK(ClassifyQuickCheck(std::string("ok"), SQLITE_ROW) == QuickCheckVerdict::Ok);
    CHECK(ClassifyQuickCheck(std::string("*** in database main ***\nPage 5: btreeInitPage() returns error code 11"),
                             SQLITE_ROW) == QuickCheckVerdict::Corrupt);
    CHECK(ClassifyQuickCheck(std::nullopt, SQLITE_CORRUPT) == QuickCheckVerdict::Corrupt);
    CHECK(ClassifyQuickCheck(std::nullopt, SQLITE_NOTADB) == QuickCheckVerdict::Corrupt);
    CHECK(ClassifyQuickCheck(std::nullopt, SQLITE_CORRUPT_INDEX) == QuickCheckVerdict::Corrupt);  // extended code
    CHECK(ClassifyQuickCheck(std::nullopt, SQLITE_BUSY) == QuickCheckVerdict::Error);
    CHECK(ClassifyQuickCheck(std::nullopt, SQLITE_LOCKED) == QuickCheckVerdict::Error);
    CHECK(ClassifyQuickCheck(std::nullopt, SQLITE_NOMEM) == QuickCheckVerdict::Error);
    CHECK(ClassifyQuickCheck(std::nullopt, SQLITE_IOERR_READ) == QuickCheckVerdict::Error);
    CHECK(ClassifyQuickCheck(std::nullopt, SQLITE_DONE) == QuickCheckVerdict::Error);  // no row at all
}

TEST_CASE("Open on a database another connection has locked fails and leaves the file in place") {
    TempDb t;
    const auto h = UtcOf(2026, 10, 18, 10);
    {
        UsageStore st;
        REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
        UsageRow r = Row(h, Series::Adapter, L"A", L"", 5, 6);
        REQUIRE(st.AddUsage(std::span<const UsageRow>(&r, 1)));
    }
    // Exclusive locking mode keeps every other connection out, readers included, until it ends.
    sqlite3* other = nullptr;
    REQUIRE(sqlite3_open(t.file.string().c_str(), &other) == SQLITE_OK);
    REQUIRE(sqlite3_exec(other, "PRAGMA locking_mode=EXCLUSIVE; BEGIN EXCLUSIVE;", nullptr, nullptr, nullptr) ==
            SQLITE_OK);

    UsageStore st;
    CHECK(st.Open(t.file) == UsageStore::OpenResult::Failed);
    CHECK(st.RecoveredFile().empty());
    for (const auto& e : fs::directory_iterator(t.dir))
        CHECK(e.path().filename().wstring().rfind(L"usage.corrupt-", 0) == std::wstring::npos);

    REQUIRE(sqlite3_exec(other, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(other);

    CHECK(st.Open(t.file) == UsageStore::OpenResult::Ok);      // the retry finds the history intact
    auto rows = st.QueryHourly({h, h + 1});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].rx == 5);
}

TEST_CASE("AddUsage returns true and the store stays healthy") {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    UsageRow r = Row(UtcOf(2026, 10, 18, 10), Series::Adapter, L"A", L"", 1, 1);
    CHECK(st.AddUsage(std::span<const UsageRow>(&r, 1)));
    CHECK(st.Healthy());
}

TEST_CASE("AddUsage reports failure, writes nothing and a retry does not double count") {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    st.SetBusyTimeoutMs(0);
    const auto h = UtcOf(2026, 10, 18, 10);
    UsageRow r = Row(h, Series::Adapter, L"A", L"", 100, 10);

    // A second connection holds the write lock, so BEGIN IMMEDIATE in the store fails with SQLITE_BUSY.
    sqlite3* other = nullptr;
    REQUIRE(sqlite3_open(t.file.string().c_str(), &other) == SQLITE_OK);
    REQUIRE(sqlite3_exec(other, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) == SQLITE_OK);

    CHECK_FALSE(st.AddUsage(std::span<const UsageRow>(&r, 1)));
    CHECK_FALSE(st.Healthy());

    REQUIRE(sqlite3_exec(other, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(other);

    CHECK(HourlyCount(st) == 0);                                // nothing was written
    CHECK(st.AddUsage(std::span<const UsageRow>(&r, 1)));       // retry of the same rows
    auto rows = st.QueryHourly({h, h + 1});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].rx == 100); CHECK(rows[0].tx == 10);          // exactly one copy
    CHECK(st.Healthy());                                        // the successful retry clears the failure
}

TEST_CASE("A failed write elsewhere is cleared by the next successful AddUsage") {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    st.SetBusyTimeoutMs(0);
    sqlite3* other = nullptr;
    REQUIRE(sqlite3_open(t.file.string().c_str(), &other) == SQLITE_OK);
    REQUIRE(sqlite3_exec(other, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) == SQLITE_OK);
    st.ClearAll();                                              // BEGIN IMMEDIATE is busy
    CHECK_FALSE(st.Healthy());
    CHECK(st.AddUsage(std::span<const UsageRow>()));            // an empty batch writes nothing...
    CHECK_FALSE(st.Healthy());                                  // ...so it proves nothing
    REQUIRE(sqlite3_exec(other, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(other);

    UsageRow r = Row(UtcOf(2026, 10, 18, 10), Series::Adapter, L"A", L"", 1, 1);
    CHECK(st.AddUsage(std::span<const UsageRow>(&r, 1)));
    CHECK(st.Healthy());
}

TEST_CASE("Text with an embedded NUL round-trips") {
    TempDb t; UsageStore st; REQUIRE(st.Open(t.file) == UsageStore::OpenResult::Ok);
    const std::wstring id(L"A\0B\u00e9", 4), key(L"n\0\u30cd", 3);
    UsageRow r{UtcOf(2026, 10, 18, 10), Series::Adapter, id, key, 1, 1};
    REQUIRE(st.AddUsage(std::span<const UsageRow>(&r, 1)));
    st.UpsertAdapter(id, std::wstring(L"Wi\0Fi", 5), AdapterKind::WiFi);
    st.SetFriendlyName(id, std::wstring(L"x\0y", 3));
    st.UpsertNetwork({key, std::wstring(L"Home\0 2", 7)});

    auto rows = st.QueryHourly({0, kFarFuture});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].adapterId == id); CHECK(rows[0].networkKey == key);
    auto a = st.Adapters();
    REQUIRE(a.size() == 1);
    CHECK(a[0].id == id);
    CHECK(a[0].name == std::wstring(L"Wi\0Fi", 5));
    CHECK(a[0].friendlyName == std::wstring(L"x\0y", 3));
    auto n = st.Networks();
    REQUIRE(n.size() == 1);
    CHECK(n[0].key == key); CHECK(n[0].name == std::wstring(L"Home\0 2", 7));
}
