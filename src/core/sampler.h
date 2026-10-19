#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/counter_tracker.h"
#include "core/types.h"

namespace nsm {

struct AdapterSample { std::wstring adapterId; CounterReading reading; };
struct UsageDelta { std::wstring adapterId; uint64_t rx = 0, tx = 0; };
struct TickResult {
    std::vector<UsageDelta> perAdapter;   // non-zero deltas only, in sample order
    std::optional<UsageDelta> internet;   // internet adapter's delta this tick, if non-zero
    std::optional<Speed> display;         // speed for the taskbar; nullopt if no display adapter
};

class Sampler {
public:
    TickResult Tick(const std::vector<AdapterSample>& samples,
                    const std::wstring& internetAdapterId,
                    const std::wstring& displayAdapterId);
private:
    std::unordered_map<std::wstring, CounterTracker> trackers_;
    std::unordered_map<std::wstring, Speed> lastSpeed_;
};

}  // namespace nsm
