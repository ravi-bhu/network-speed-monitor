#pragma once
#include <cstdint>

#include "core/types.h"

namespace nsm {

struct CounterReading { uint64_t rxBytes; uint64_t txBytes; uint64_t linkBitsPerSec; double timeSec; };
struct TrackerResult { bool speedValid = false; Speed speed; uint64_t rxDelta = 0, txDelta = 0; };

inline constexpr double kGapSeconds = 3.0;
inline constexpr uint64_t kUnknownLinkBitsPerSec = 100'000'000'000ull;

class CounterTracker {
public:
    TrackerResult Update(const CounterReading& r);
private:
    bool has_ = false;
    CounterReading prev_{};
};

}  // namespace nsm
