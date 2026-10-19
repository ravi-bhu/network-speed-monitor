#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/time_zone.h"
#include "core/types.h"

struct sqlite3;

namespace nsm {

// How Open reads the result of PRAGMA quick_check (exposed for tests). firstRow is the check's first row, or
// nullopt when preparing or stepping it failed, with errcode = sqlite3_errcode() at that point. Only corruption
// (SQLITE_CORRUPT/NOTADB, or a first row other than "ok") moves the file aside; any other failure (busy,
// locked, out of memory, I/O) is an Error that leaves it alone.
enum class QuickCheckVerdict { Ok, Corrupt, Error };
QuickCheckVerdict ClassifyQuickCheck(const std::optional<std::string>& firstRow, int errcode);

struct AdapterMeta { std::wstring id, name, friendlyName; AdapterKind kind = AdapterKind::Other; bool hidden = false; };
struct NetworkMeta { std::wstring key, name; };

// SQLite-backed usage history. Single-threaded (UI thread). No exceptions escape the public API;
// failures make the call a no-op (queries return empty).
class UsageStore {
public:
    enum class OpenResult { Ok, RecoveredFromCorruption, Failed };

    UsageStore() = default;
    ~UsageStore();
    UsageStore(const UsageStore&) = delete;
    UsageStore& operator=(const UsageStore&) = delete;

    OpenResult Open(const std::filesystem::path& file);
    std::filesystem::path RecoveredFile() const { return recovered_; }   // where the corrupt db was moved

    // Adds to existing rows in one transaction. Returns true only if the whole batch committed; on false
    // nothing was written, so the caller may safely re-add the same rows later.
    bool AddUsage(std::span<const UsageRow> rows);

    // False after any runtime write/IO failure or SQLITE_CORRUPT/NOTADB/IOERR from any call (sticky);
    // reset to true by a successful Open. Lets the app tell the user the history is not being saved.
    bool Healthy() const { return healthy_; }

    // Busy timeout applied to the connection (default 2000 ms, set by Open). Exposed for tests.
    void SetBusyTimeoutMs(int ms);

    void UpsertAdapter(const std::wstring& id, const std::wstring& name, AdapterKind kind);  // keeps friendlyName/hidden
    void UpsertNetwork(const NetworkMeta& n);
    void SetFriendlyName(const std::wstring& adapterId, const std::wstring& name);          // empty clears
    void SetHidden(const std::wstring& adapterId, bool hidden);
    std::vector<AdapterMeta> Adapters();
    std::vector<NetworkMeta> Networks();
    std::vector<UsageRow> QueryHourly(UtcRange r);                       // [begin, end)
    std::vector<DailyRow> QueryDaily(LocalDate from, LocalDate to);      // inclusive
    void RollUp(int64_t nowUtc, const TimeZone& tz);                     // hourly rows older than 90 days -> daily
    void ClearAll();
    void ClearAdapter(const std::wstring& id);
    void ClearNetwork(const std::wstring& key);

private:
    enum class Attempt { Ok, Corrupt, Error };
    Attempt TryOpen(const std::filesystem::path& file);
    void Close();
    void CheckError();
    template <class F> bool Write(F&& f) noexcept;

    sqlite3* db_ = nullptr;
    bool healthy_ = true;
    std::filesystem::path recovered_;
};

}  // namespace nsm
