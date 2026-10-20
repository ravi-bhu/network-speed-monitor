#pragma once
#include <cstddef>
#include <vector>

#include "core/types.h"

namespace nsm {

// Fixed-capacity ring of recent speed samples (bytes/s), oldest first on read.
class SpeedHistory {
public:
    explicit SpeedHistory(size_t capacity = 60);
    void Push(Speed s);
    void Clear();  // forget every sample; the capacity stays
    size_t Size() const;
    double Max() const;  // max over rx and tx, never below 1000.0
    std::vector<double> Rx() const;
    std::vector<double> Tx() const;

private:
    std::vector<double> Column(double Speed::*field) const;  // one field of every sample, oldest first

    size_t capacity_;
    size_t head_ = 0;  // index of the oldest sample once full
    std::vector<Speed> samples_;
};

}  // namespace nsm
