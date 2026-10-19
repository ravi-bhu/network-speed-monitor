#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "core/types.h"

namespace nsm {

// Collects usage deltas in memory, summed per (hour, series, adapter, network).
class UsageAccumulator {
public:
    void Add(int64_t nowUtc, Series s, const std::wstring& adapterId, const std::wstring& networkKey,
             uint64_t rx, uint64_t tx);
    std::vector<UsageRow> Take();   // returns everything accumulated and empties the accumulator
    bool Empty() const { return rows_.empty(); }

private:
    using Key = std::tuple<int64_t, int, std::wstring, std::wstring>;
    std::map<Key, std::pair<uint64_t, uint64_t>> rows_;
};

// True when at least 300 s have elapsed since the last flush, or the UTC hour changed.
bool ShouldFlush(int64_t lastFlushUtc, int64_t nowUtc);

}  // namespace nsm
