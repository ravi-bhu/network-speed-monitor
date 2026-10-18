#include "doctest.h"

#include "core/units.h"

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
