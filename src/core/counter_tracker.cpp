#include "core/counter_tracker.h"

namespace nsm {

TrackerResult CounterTracker::Update(const CounterReading& r) {
    TrackerResult out;
    if (!has_) { prev_ = r; has_ = true; return out; }

    const double dt = r.timeSec - prev_.timeSec;
    if (dt <= 0) return out;

    const CounterReading prev = prev_;
    prev_ = r;
    if (r.rxBytes < prev.rxBytes || r.txBytes < prev.txBytes) return out;

    const uint64_t rxD = r.rxBytes - prev.rxBytes;
    const uint64_t txD = r.txBytes - prev.txBytes;
    const double cap = static_cast<double>(r.linkBitsPerSec ? r.linkBitsPerSec : kUnknownLinkBitsPerSec) / 8.0 * dt * kPlausibilityMargin;
    if (static_cast<double>(rxD) > cap || static_cast<double>(txD) > cap) return out;

    out.rxDelta = rxD;
    out.txDelta = txD;
    if (dt > kGapSeconds) return out;
    out.speedValid = true;
    out.speed.rx = rxD / dt;
    out.speed.tx = txD / dt;
    return out;
}

}  // namespace nsm
