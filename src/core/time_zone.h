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
    // Nonexistent (spring-forward gap) or ambiguous (fall-back repeat) local times
    // resolve to the earliest valid instant.
    int64_t ToUtc(const LocalDateTime& t) const;

private:
    struct Impl;
    explicit TimeZone(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<const Impl> impl_;
};

}  // namespace nsm
