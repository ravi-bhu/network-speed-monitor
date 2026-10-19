#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "core/time_zone.h"
#include "core/types.h"

struct sqlite3;

namespace nsm {

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

    void AddUsage(std::span<const UsageRow> rows);                       // adds to existing rows, one transaction
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

    sqlite3* db_ = nullptr;
    std::filesystem::path recovered_;
};

}  // namespace nsm
