#include "core/units.h"

#include <cmath>
#include <cstdio>

namespace nsm {
namespace {

std::wstring Scale(double v, const wchar_t* const* units, int unitCount, const wchar_t* separator = L" ") {
    if (!(v > 0) || !std::isfinite(v)) v = 0;  // NaN, infinite or negative
    int idx = 0;
    while (v >= 1000.0 && idx < unitCount - 1) { v /= 1000.0; ++idx; }

    auto decimalsFor = [](int i, double x) { return i == 0 ? 0 : x < 10 ? 2 : x < 100 ? 1 : 0; };
    auto roundTo = [](double x, int d) { double p = std::pow(10.0, d); return std::round(x * p) / p; };

    int d = decimalsFor(idx, v);
    double r = roundTo(v, d);
    if (r >= 1000.0 && idx < unitCount - 1) {  // rounding crossed a unit boundary
        v = r / 1000.0;
        ++idx;
        d = decimalsFor(idx, v);
        r = roundTo(v, d);
    } else if (decimalsFor(idx, r) < d) {  // rounding crossed 10 or 100: keep ~3 significant figures
        d = decimalsFor(idx, r);
        r = roundTo(v, d);
    }
    wchar_t buf[64];
    swprintf(buf, std::size(buf), L"%.*f%ls%ls", d, r, separator, units[idx]);
    return buf;
}

}  // namespace

std::wstring FormatSpeed(double bytesPerSec, UnitMode mode) {
    static const wchar_t* const bytes[] = {L"B/s", L"KB/s", L"MB/s", L"GB/s"};
    static const wchar_t* const bits[] = {L"bps", L"Kbps", L"Mbps", L"Gbps"};
    if (mode == UnitMode::Bits) return Scale(bytesPerSec * 8.0, bits, 4);
    return Scale(bytesPerSec, bytes, 4);
}

std::wstring FormatSpeedCompact(double bytesPerSec, UnitMode mode) {
    static const wchar_t* const bytes[] = {L"B", L"KB", L"MB", L"GB"};
    static const wchar_t* const bits[] = {L"b", L"Kb", L"Mb", L"Gb"};
    if (mode == UnitMode::Bits) return Scale(bytesPerSec * 8.0, bits, 4, L"");
    return Scale(bytesPerSec, bytes, 4, L"");
}

std::wstring FormatVolume(uint64_t b) {
    static const wchar_t* const units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    return Scale(static_cast<double>(b), units, 5);
}

}  // namespace nsm
