#include "core/speed_history.h"

#include <algorithm>

namespace nsm {

SpeedHistory::SpeedHistory(size_t capacity) : capacity_(capacity ? capacity : 1) {
    samples_.reserve(capacity_);
}

void SpeedHistory::Push(Speed s) {
    if (samples_.size() < capacity_) {
        samples_.push_back(s);
    } else {
        samples_[head_] = s;
        head_ = (head_ + 1) % capacity_;
    }
}

size_t SpeedHistory::Size() const { return samples_.size(); }

double SpeedHistory::Max() const {
    double m = 1000.0;
    for (const Speed& s : samples_) m = std::max({m, s.rx, s.tx});
    return m;
}

std::vector<double> SpeedHistory::Rx() const { return Column(&Speed::rx); }

std::vector<double> SpeedHistory::Tx() const { return Column(&Speed::tx); }

std::vector<double> SpeedHistory::Column(double Speed::*field) const {
    std::vector<double> out;
    out.reserve(samples_.size());
    for (size_t i = 0; i < samples_.size(); ++i)
        out.push_back(samples_[(head_ + i) % samples_.size()].*field);
    return out;
}

}  // namespace nsm
