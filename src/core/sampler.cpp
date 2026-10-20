#include "core/sampler.h"

#include <unordered_set>

namespace nsm {

TickResult Sampler::Tick(const std::vector<AdapterSample>& samples,
                         const std::wstring& internetAdapterId,
                         const std::wstring& displayAdapterId) {
    TickResult out;

    // Drop state for adapters that are no longer present so they restart fresh.
    std::unordered_set<std::wstring> present;
    present.reserve(samples.size());
    for (const auto& s : samples) present.insert(s.adapterId);
    for (auto it = trackers_.begin(); it != trackers_.end();) {
        if (present.contains(it->first)) ++it;
        else { lastSpeed_.erase(it->first); it = trackers_.erase(it); }
    }

    for (const auto& s : samples) {
        const TrackerResult r = trackers_[s.adapterId].Update(s.reading);
        if (r.speedValid) lastSpeed_[s.adapterId] = r.speed;

        if (r.rxDelta != 0 || r.txDelta != 0) {
            UsageDelta d{s.adapterId, r.rxDelta, r.txDelta};
            if (!internetAdapterId.empty() && s.adapterId == internetAdapterId) out.internet = d;
            out.perAdapter.push_back(std::move(d));
        }

        if (!displayAdapterId.empty() && s.adapterId == displayAdapterId) {
            if (r.speedValid) out.display = r.speed;
            else {
                auto it = lastSpeed_.find(s.adapterId);
                out.display = it != lastSpeed_.end() ? it->second : Speed{};
            }
        }
    }
    return out;
}

}  // namespace nsm
