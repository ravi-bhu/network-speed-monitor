#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/time_zone.h"
#include "core/types.h"
#include "storage/usage_store.h"

namespace nsm {

struct Totals {
    uint64_t rx = 0, tx = 0;
    uint64_t Total() const { return rx + tx; }
};

struct NamedTotals {
    std::wstring id;
    std::wstring name;   // empty name = unknown network
    AdapterKind kind = AdapterKind::Other;
    bool hidden = false;
    Totals totals;
};

enum class Granularity { Hour, Day, Month };

struct TimelineBar {
    LocalDateTime start;
    Totals totals;
    // Hour bars: the bucket's length, under an hour next to a part-hour DST change (Chatham's 02:00 bucket
    // lasts 45 minutes). 0 for day and month bars.
    int64_t lengthSec = 0;
};

struct SummaryData {
    Totals today, thisWeek, thisMonth, period;
    std::optional<std::pair<LocalDate, Totals>> busiestDay;
    std::vector<NamedTotals> topAdapters, topNetworks;   // top 3 in the period
};

// Read-only view over the usage store. Headline numbers (summary cards, busiest day, timeline) use the
// internet series; the adapter and Wi-Fi tables use the raw per-adapter series.
class ReportModel {
public:
    ReportModel(UsageStore& store, const TimeZone& tz, int firstDayOfWeek);

    SummaryData Summary(UtcRange period, int64_t nowUtc);
    Granularity PickGranularity(UtcRange r, int64_t nowUtc);
    std::vector<TimelineBar> Timeline(UtcRange r, int64_t nowUtc);
    std::vector<NamedTotals> AdapterTable(UtcRange r, bool includeHidden);
    std::vector<NamedTotals> WifiTable(UtcRange r);

private:
    UsageStore& store_;
    TimeZone tz_;
    int firstDayOfWeek_;
};

}  // namespace nsm
