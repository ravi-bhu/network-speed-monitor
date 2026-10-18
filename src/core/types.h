#pragma once
#include <compare>
#include <cstdint>
#include <string>

namespace nsm {

enum class AdapterKind { WiFi, Ethernet, Virtual, Other };
enum class Series { Adapter = 0, Internet = 1 };
enum class UnitMode { Bits, Bytes };

struct Speed { double rx = 0, tx = 0; };            // bytes per second
struct UtcRange { int64_t begin = 0, end = 0; };    // [begin, end), unix seconds

struct LocalDate {
    int year = 0, month = 0, day = 0;
    auto operator<=>(const LocalDate&) const = default;
};

struct UsageRow {
    int64_t hourUtc;
    Series series;
    std::wstring adapterId;
    std::wstring networkKey;
    uint64_t rx, tx;
};

struct DailyRow {
    LocalDate day;
    Series series;
    std::wstring adapterId;
    std::wstring networkKey;
    uint64_t rx, tx;
};

}  // namespace nsm
