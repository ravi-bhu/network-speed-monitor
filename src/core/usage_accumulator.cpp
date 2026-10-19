#include "core/usage_accumulator.h"

#include "core/periods.h"

namespace nsm {

void UsageAccumulator::Add(int64_t hourStartUtc, Series s, const std::wstring& adapterId,
                           const std::wstring& networkKey, uint64_t rx, uint64_t tx) {
    auto& v = rows_[Key{hourStartUtc, static_cast<int>(s), adapterId, networkKey}];
    v.first += rx;
    v.second += tx;
}

std::vector<UsageRow> UsageAccumulator::Take() {
    std::vector<UsageRow> out;
    out.reserve(rows_.size());
    for (auto& [k, v] : rows_) {
        out.push_back(UsageRow{std::get<0>(k), static_cast<Series>(std::get<1>(k)), std::get<2>(k),
                               std::get<3>(k), v.first, v.second});
    }
    rows_.clear();
    return out;
}

bool ShouldFlush(int64_t lastFlushUtc, int64_t nowUtc, const TimeZone& tz) {
    return nowUtc - lastFlushUtc >= 300 || LocalHourStart(lastFlushUtc, tz) != LocalHourStart(nowUtc, tz);
}

}  // namespace nsm
