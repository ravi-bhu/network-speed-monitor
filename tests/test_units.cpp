#include "doctest.h"

#include "core/units.h"

#include <limits>

using namespace nsm;

TEST_CASE("FormatSpeed bytes auto-scales to ~3 significant figures") {
    CHECK(FormatSpeed(0, UnitMode::Bytes) == L"0 B/s");
    CHECK(FormatSpeed(999, UnitMode::Bytes) == L"999 B/s");
    CHECK(FormatSpeed(1000, UnitMode::Bytes) == L"1.00 KB/s");
    CHECK(FormatSpeed(950'000, UnitMode::Bytes) == L"950 KB/s");
    CHECK(FormatSpeed(1'240'000, UnitMode::Bytes) == L"1.24 MB/s");
    CHECK(FormatSpeed(15'800'000, UnitMode::Bytes) == L"15.8 MB/s");
    CHECK(FormatSpeed(999'600, UnitMode::Bytes) == L"1.00 MB/s");   // rounding crosses a unit boundary
    CHECK(FormatSpeed(1'250'000'000, UnitMode::Bytes) == L"1.25 GB/s");
    CHECK(FormatSpeed(2e12, UnitMode::Bytes) == L"2000 GB/s");      // GB is the largest unit
    CHECK(FormatSpeed(-5, UnitMode::Bytes) == L"0 B/s");
}

TEST_CASE("FormatSpeed bits multiplies by 8 and uses bps units") {
    CHECK(FormatSpeed(100, UnitMode::Bits) == L"800 bps");
    CHECK(FormatSpeed(125, UnitMode::Bits) == L"1.00 Kbps");
    CHECK(FormatSpeed(1'240'000, UnitMode::Bits) == L"9.92 Mbps");
    CHECK(FormatSpeed(1'250'000'000, UnitMode::Bits) == L"10.0 Gbps");
}

TEST_CASE("FormatVolume uses decimal byte units up to TB") {
    CHECK(FormatVolume(0) == L"0 B");
    CHECK(FormatVolume(999) == L"999 B");
    CHECK(FormatVolume(64'700'000'000ull) == L"64.7 GB");
    CHECK(FormatVolume(1'500'000'000'000ull) == L"1.50 TB");
}

TEST_CASE("FormatSpeed keeps ~3 significant figures when rounding crosses 10 or 100 (#2)") {
    CHECK(FormatSpeed(9'996, UnitMode::Bytes) == L"10.0 KB/s");
    CHECK(FormatSpeed(99'960, UnitMode::Bytes) == L"100 KB/s");
    CHECK(FormatSpeed(9.996e6, UnitMode::Bytes) == L"10.0 MB/s");
    CHECK(FormatSpeed(1'249'500, UnitMode::Bits) == L"10.0 Mbps");  // 9.996 Mbps
}

TEST_CASE("FormatSpeed treats non-finite input as zero (#2)") {
    CHECK(FormatSpeed(std::numeric_limits<double>::infinity(), UnitMode::Bytes) == L"0 B/s");
    CHECK(FormatSpeed(std::numeric_limits<double>::quiet_NaN(), UnitMode::Bits) == L"0 bps");
}

TEST_CASE("FormatSpeedCompact drops the space and the /s for narrow taskbars (#30)") {
    CHECK(FormatSpeedCompact(0, UnitMode::Bytes) == L"0B");
    CHECK(FormatSpeedCompact(950'000, UnitMode::Bytes) == L"950KB");
    CHECK(FormatSpeedCompact(1'240'000, UnitMode::Bytes) == L"1.24MB");
    CHECK(FormatSpeedCompact(15'800'000, UnitMode::Bytes) == L"15.8MB");
    CHECK(FormatSpeedCompact(1'240'000, UnitMode::Bits) == L"9.92Mb");
    CHECK(FormatSpeedCompact(100, UnitMode::Bits) == L"800b");
    CHECK(FormatSpeedCompact(99'960, UnitMode::Bytes) == L"100KB");
}
