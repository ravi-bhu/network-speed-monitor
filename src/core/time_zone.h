#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace nsm {

struct LocalDateTime { int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0; };

// A Windows dynamic time zone. Copies share the (immutable) zone data.
class TimeZone {
public:
    static TimeZone Local();                                         // current system zone
    static std::optional<TimeZone> FromKey(const std::wstring& key); // e.g. L"Pacific Standard Time"

    LocalDateTime ToLocal(int64_t utcSec) const;
    // Ambiguous local time (fall-back repeat): the earlier of the two instants.
    // Nonexistent local time (inside a spring-forward gap): the transition instant,
    // i.e. the first valid instant after the gap.
    int64_t ToUtc(const LocalDateTime& t) const;

private:
    struct Impl;
    explicit TimeZone(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<const Impl> impl_;
};

}  // namespace nsm
