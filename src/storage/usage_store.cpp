#include "storage/usage_store.h"

#include <windows.h>

#include <cstdio>
#include <map>
#include <tuple>

#include "core/periods.h"
#include "sqlite3.h"

namespace nsm {

namespace {

constexpr int64_t kRollUpAgeSeconds = 90LL * 86400;

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const char* s) {
    if (!s || !*s) return {};
    const int len = static_cast<int>(strlen(s));
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, len, nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, len, w.data(), n);
    return w;
}

std::string DayString(const LocalDate& d) {
    char buf[32];
    snprintf(buf, sizeof buf, "%04d-%02d-%02d", d.year, d.month, d.day);
    return buf;
}

LocalDate ParseDay(const char* s) {
    LocalDate d;
    if (s) sscanf_s(s, "%d-%d-%d", &d.year, &d.month, &d.day);
    return d;
}

// Runs f, swallowing any exception (the public API never throws).
template <class F>
void Guard(F&& f) noexcept {
    try { f(); } catch (...) {}
}

// Prepared statement with bound parameters. Binding indexes are 1-based.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK) s_ = nullptr;
    }
    ~Stmt() { sqlite3_finalize(s_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool ok() const { return s_ != nullptr; }
    Stmt& Int(int i, int64_t v) { if (s_) sqlite3_bind_int64(s_, i, v); return *this; }
    Stmt& Text(int i, const std::wstring& v) {
        if (s_) {
            const std::string u = ToUtf8(v);
            sqlite3_bind_text(s_, i, u.c_str(), static_cast<int>(u.size()), SQLITE_TRANSIENT);
        }
        return *this;
    }
    Stmt& Text(int i, const std::string& v) {
        if (s_) sqlite3_bind_text(s_, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& Null(int i) { if (s_) sqlite3_bind_null(s_, i); return *this; }

    bool Step() { return s_ && sqlite3_step(s_) == SQLITE_ROW; }          // true while rows remain
    bool Run() {                                                          // executes a non-query; resets for reuse
        if (!s_) return false;
        const int rc = sqlite3_step(s_);
        sqlite3_reset(s_);
        sqlite3_clear_bindings(s_);
        return rc == SQLITE_DONE || rc == SQLITE_ROW;
    }
    int64_t ColInt(int i) const { return sqlite3_column_int64(s_, i); }
    std::wstring ColText(int i) const {
        return FromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s_, i)));
    }
    std::string ColUtf8(int i) const {
        const auto* p = sqlite3_column_text(s_, i);
        return p ? reinterpret_cast<const char*>(p) : std::string();
    }

private:
    sqlite3_stmt* s_ = nullptr;
};

bool Exec(sqlite3* db, const char* sql) { return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }

// BEGIN IMMEDIATE ... COMMIT; rolls back unless Commit() succeeded.
class Transaction {
public:
    explicit Transaction(sqlite3* db) : db_(db), open_(Exec(db, "BEGIN IMMEDIATE")) {}
    ~Transaction() { if (open_) Exec(db_, "ROLLBACK"); }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    bool started() const { return open_; }
    bool Commit() {
        if (!open_) return false;
        open_ = false;
        if (Exec(db_, "COMMIT")) return true;
        Exec(db_, "ROLLBACK");
        return false;
    }

private:
    sqlite3* db_;
    bool open_;
};

constexpr const char* kSchema =
    "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS adapters(id TEXT PRIMARY KEY, name TEXT NOT NULL, kind INTEGER NOT NULL,"
    " friendly_name TEXT, hidden INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS networks(key TEXT PRIMARY KEY, name TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS usage_hourly(hour_utc INTEGER NOT NULL, series INTEGER NOT NULL,"
    " adapter_id TEXT NOT NULL, network_key TEXT NOT NULL DEFAULT '', rx INTEGER NOT NULL, tx INTEGER NOT NULL,"
    " PRIMARY KEY(hour_utc, series, adapter_id, network_key)) WITHOUT ROWID;"
    "CREATE TABLE IF NOT EXISTS usage_daily(day TEXT NOT NULL, series INTEGER NOT NULL,"
    " adapter_id TEXT NOT NULL, network_key TEXT NOT NULL DEFAULT '', rx INTEGER NOT NULL, tx INTEGER NOT NULL,"
    " PRIMARY KEY(day, series, adapter_id, network_key)) WITHOUT ROWID;"
    "INSERT OR IGNORE INTO meta(key, value) VALUES('schema_version', '1');";

bool IsCorruptionCode(int rc) {
    const int primary = rc & 0xff;
    return primary == SQLITE_CORRUPT || primary == SQLITE_NOTADB;
}

void Rename(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
}

}  // namespace

QuickCheckVerdict ClassifyQuickCheck(const std::optional<std::string>& firstRow, int errcode) {
    if (firstRow) return *firstRow == "ok" ? QuickCheckVerdict::Ok : QuickCheckVerdict::Corrupt;
    return IsCorruptionCode(errcode) ? QuickCheckVerdict::Corrupt : QuickCheckVerdict::Error;
}

UsageStore::~UsageStore() { Close(); }

void UsageStore::SetBusyTimeoutMs(int ms) {
    if (db_) sqlite3_busy_timeout(db_, ms);
}

// Marks the store unhealthy if the last call on the connection reported corruption or an I/O error.
void UsageStore::CheckError() {
    if (!db_) return;
    if (IsCorruptionCode(sqlite3_errcode(db_)) || (sqlite3_errcode(db_) & 0xff) == SQLITE_IOERR) healthy_ = false;
}

// Runs a write operation; a false result or an exception makes the store unhealthy. Returns success.
template <class F>
bool UsageStore::Write(F&& f) noexcept {
    bool ok = false;
    try { ok = db_ && f(); } catch (...) { ok = false; }
    if (!ok) healthy_ = false;
    CheckError();
    return ok;
}

void UsageStore::Close() {
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

UsageStore::Attempt UsageStore::TryOpen(const std::filesystem::path& file) {
    const std::string path = ToUtf8(file.wstring());
    int rc = sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) return IsCorruptionCode(rc) ? Attempt::Corrupt : Attempt::Error;
    sqlite3_busy_timeout(db_, 2000);

    // A garbage file is first detected by the first statement that reads the header.
    rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) return IsCorruptionCode(rc) ? Attempt::Corrupt : Attempt::Error;
    if (!Exec(db_, "PRAGMA synchronous=NORMAL;")) return Attempt::Error;

    {
        // A busy, locked or out-of-memory check says nothing about the file: only corruption moves it aside.
        Stmt check(db_, "PRAGMA quick_check;");
        std::optional<std::string> firstRow;
        if (check.ok() && check.Step()) firstRow = check.ColUtf8(0);
        switch (ClassifyQuickCheck(firstRow, sqlite3_errcode(db_))) {
            case QuickCheckVerdict::Ok: break;
            case QuickCheckVerdict::Corrupt: return Attempt::Corrupt;
            case QuickCheckVerdict::Error: return Attempt::Error;
        }
    }

    rc = sqlite3_exec(db_, kSchema, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) return IsCorruptionCode(rc) ? Attempt::Corrupt : Attempt::Error;
    return Attempt::Ok;
}

UsageStore::OpenResult UsageStore::Open(const std::filesystem::path& file) {
    try {
        Close();
        recovered_.clear();
        healthy_ = true;

        std::error_code ec;
        if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);

        Attempt a = TryOpen(file);
        if (a == Attempt::Ok) return OpenResult::Ok;
        Close();
        healthy_ = false;
        if (a == Attempt::Error) return OpenResult::Failed;

        // Corrupt: move the database (and its WAL files) aside and start fresh.
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t stamp[32];
        swprintf_s(stamp, L"%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        std::filesystem::path target;
        for (int n = 0; n < 100; ++n) {
            std::wstring name = std::wstring(L"usage.corrupt-") + stamp;
            if (n > 0) name += L"-" + std::to_wstring(n);
            name += L".db";
            target = file.parent_path() / name;
            if (!std::filesystem::exists(target, ec)) break;
        }
        std::filesystem::rename(file, target, ec);
        if (ec) return OpenResult::Failed;
        Rename(file.wstring() + L"-wal", target.wstring() + L"-wal");
        Rename(file.wstring() + L"-shm", target.wstring() + L"-shm");

        a = TryOpen(file);
        if (a != Attempt::Ok) { Close(); return OpenResult::Failed; }
        recovered_ = target;
        healthy_ = true;
        return OpenResult::RecoveredFromCorruption;
    } catch (...) {
        Close();
        healthy_ = false;
        return OpenResult::Failed;
    }
}

bool UsageStore::AddUsage(std::span<const UsageRow> rows) {
    if (rows.empty()) return db_ != nullptr;
    return Write([&] {
        Transaction tx(db_);
        if (!tx.started()) return false;
        Stmt ins(db_,
            "INSERT INTO usage_hourly(hour_utc, series, adapter_id, network_key, rx, tx) VALUES(?1,?2,?3,?4,?5,?6)"
            " ON CONFLICT(hour_utc, series, adapter_id, network_key)"
            " DO UPDATE SET rx = rx + excluded.rx, tx = tx + excluded.tx");
        if (!ins.ok()) return false;
        for (const UsageRow& r : rows) {
            ins.Int(1, r.hourUtc).Int(2, static_cast<int>(r.series)).Text(3, r.adapterId).Text(4, r.networkKey)
               .Int(5, static_cast<int64_t>(r.rx)).Int(6, static_cast<int64_t>(r.tx));
            if (!ins.Run()) return false;
        }
        return tx.Commit();
    });
}

void UsageStore::UpsertAdapter(const std::wstring& id, const std::wstring& name, AdapterKind kind) {
    Write([&] {
        Stmt s(db_,
            "INSERT INTO adapters(id, name, kind) VALUES(?1,?2,?3)"
            " ON CONFLICT(id) DO UPDATE SET name = excluded.name, kind = excluded.kind");
        s.Text(1, id).Text(2, name).Int(3, static_cast<int>(kind));
        return s.Run();
    });
}

void UsageStore::UpsertNetwork(const NetworkMeta& n) {
    Write([&] {
        Stmt s(db_,
            "INSERT INTO networks(key, name) VALUES(?1,?2)"
            " ON CONFLICT(key) DO UPDATE SET name = excluded.name");
        s.Text(1, n.key).Text(2, n.name);
        return s.Run();
    });
}

void UsageStore::SetFriendlyName(const std::wstring& adapterId, const std::wstring& name) {
    Write([&] {
        Stmt s(db_, "UPDATE adapters SET friendly_name = ?2 WHERE id = ?1");
        s.Text(1, adapterId);
        if (name.empty()) s.Null(2); else s.Text(2, name);
        return s.Run();
    });
}

void UsageStore::SetHidden(const std::wstring& adapterId, bool hidden) {
    Write([&] {
        Stmt s(db_, "UPDATE adapters SET hidden = ?2 WHERE id = ?1");
        s.Text(1, adapterId).Int(2, hidden ? 1 : 0);
        return s.Run();
    });
}

std::vector<AdapterMeta> UsageStore::Adapters() {
    std::vector<AdapterMeta> out;
    Guard([&] {
        if (!db_) return;
        Stmt s(db_, "SELECT id, name, kind, friendly_name, hidden FROM adapters ORDER BY id");
        while (s.Step()) {
            AdapterMeta a;
            a.id = s.ColText(0); a.name = s.ColText(1);
            a.kind = static_cast<AdapterKind>(s.ColInt(2));
            a.friendlyName = s.ColText(3);
            a.hidden = s.ColInt(4) != 0;
            out.push_back(std::move(a));
        }
    });
    CheckError();
    return out;
}

std::vector<NetworkMeta> UsageStore::Networks() {
    std::vector<NetworkMeta> out;
    Guard([&] {
        if (!db_) return;
        Stmt s(db_, "SELECT key, name FROM networks ORDER BY key");
        while (s.Step()) out.push_back(NetworkMeta{s.ColText(0), s.ColText(1)});
    });
    CheckError();
    return out;
}

std::vector<UsageRow> UsageStore::QueryHourly(UtcRange r) {
    std::vector<UsageRow> out;
    Guard([&] {
        if (!db_) return;
        Stmt s(db_,
            "SELECT hour_utc, series, adapter_id, network_key, rx, tx FROM usage_hourly"
            " WHERE hour_utc >= ?1 AND hour_utc < ?2 ORDER BY hour_utc, series, adapter_id, network_key");
        s.Int(1, r.begin).Int(2, r.end);
        while (s.Step()) {
            out.push_back(UsageRow{s.ColInt(0), static_cast<Series>(s.ColInt(1)), s.ColText(2), s.ColText(3),
                                   static_cast<uint64_t>(s.ColInt(4)), static_cast<uint64_t>(s.ColInt(5))});
        }
    });
    CheckError();
    return out;
}

std::vector<DailyRow> UsageStore::QueryDaily(LocalDate from, LocalDate to) {
    std::vector<DailyRow> out;
    Guard([&] {
        if (!db_) return;
        Stmt s(db_,
            "SELECT day, series, adapter_id, network_key, rx, tx FROM usage_daily"
            " WHERE day >= ?1 AND day <= ?2 ORDER BY day, series, adapter_id, network_key");
        s.Text(1, DayString(from)).Text(2, DayString(to));
        while (s.Step()) {
            out.push_back(DailyRow{ParseDay(s.ColUtf8(0).c_str()), static_cast<Series>(s.ColInt(1)), s.ColText(2),
                                   s.ColText(3), static_cast<uint64_t>(s.ColInt(4)),
                                   static_cast<uint64_t>(s.ColInt(5))});
        }
    });
    CheckError();
    return out;
}

void UsageStore::RollUp(int64_t nowUtc, const TimeZone& tz) {
    Write([&] {
        // The same cutoff as ReportModel::PickGranularity, on local-hour buckets.
        const int64_t cutoff = LocalHourStart(nowUtc, tz) - kRollUpAgeSeconds;

        using Key = std::tuple<std::string, int, std::wstring, std::wstring>;
        std::map<Key, std::pair<uint64_t, uint64_t>> sums;
        {
            Stmt sel(db_,
                "SELECT hour_utc, series, adapter_id, network_key, rx, tx FROM usage_hourly WHERE hour_utc < ?1");
            if (!sel.ok()) return false;
            sel.Int(1, cutoff);
            while (sel.Step()) {
                auto& v = sums[Key{DayString(LocalDateOf(sel.ColInt(0), tz)), static_cast<int>(sel.ColInt(1)),
                                   sel.ColText(2), sel.ColText(3)}];
                v.first += static_cast<uint64_t>(sel.ColInt(4));
                v.second += static_cast<uint64_t>(sel.ColInt(5));
            }
        }
        if (sums.empty()) return true;

        Transaction tx(db_);
        if (!tx.started()) return false;
        Stmt ins(db_,
            "INSERT INTO usage_daily(day, series, adapter_id, network_key, rx, tx) VALUES(?1,?2,?3,?4,?5,?6)"
            " ON CONFLICT(day, series, adapter_id, network_key)"
            " DO UPDATE SET rx = rx + excluded.rx, tx = tx + excluded.tx");
        if (!ins.ok()) return false;
        for (const auto& [k, v] : sums) {
            ins.Text(1, std::get<0>(k)).Int(2, std::get<1>(k)).Text(3, std::get<2>(k)).Text(4, std::get<3>(k))
               .Int(5, static_cast<int64_t>(v.first)).Int(6, static_cast<int64_t>(v.second));
            if (!ins.Run()) return false;
        }
        Stmt del(db_, "DELETE FROM usage_hourly WHERE hour_utc < ?1");
        del.Int(1, cutoff);
        if (!del.Run()) return false;
        return tx.Commit();
    });
}

void UsageStore::ClearAll() {
    Write([&] {
        Transaction tx(db_);
        if (!tx.started()) return false;
        if (!Exec(db_, "DELETE FROM usage_hourly") || !Exec(db_, "DELETE FROM usage_daily")) return false;
        return tx.Commit();
    });
}

void UsageStore::ClearAdapter(const std::wstring& id) {
    Write([&] {
        Transaction tx(db_);
        if (!tx.started()) return false;
        Stmt h(db_, "DELETE FROM usage_hourly WHERE adapter_id = ?1");
        Stmt d(db_, "DELETE FROM usage_daily WHERE adapter_id = ?1");
        if (!h.Text(1, id).Run() || !d.Text(1, id).Run()) return false;
        return tx.Commit();
    });
}

void UsageStore::ClearNetwork(const std::wstring& key) {
    Write([&] {
        Transaction tx(db_);
        if (!tx.started()) return false;
        Stmt h(db_, "DELETE FROM usage_hourly WHERE network_key = ?1");
        Stmt d(db_, "DELETE FROM usage_daily WHERE network_key = ?1");
        if (!h.Text(1, key).Run() || !d.Text(1, key).Run()) return false;
        return tx.Commit();
    });
}

}  // namespace nsm
