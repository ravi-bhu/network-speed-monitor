#include "core/units.h"

#include <cmath>
#include <cstdio>

namespace nsm {
namespace {

std::wstring Scale(double v, const wchar_t* const* units, int unitCount) {
    if (!(v > 0)) v = 0;  // NaN or negative
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
    }
    wchar_t buf[64];
    swprintf(buf, std::size(buf), L"%.*f %ls", d, r, units[idx]);
    return buf;
}

}  // namespace

std::wstring FormatSpeed(double bytesPerSec, UnitMode mode) {
    static const wchar_t* const bytes[] = {L"B/s", L"KB/s", L"MB/s", L"GB/s"};
    static const wchar_t* const bits[] = {L"bps", L"Kbps", L"Mbps", L"Gbps"};
    if (mode == UnitMode::Bits) return Scale(bytesPerSec * 8.0, bits, 4);
    return Scale(bytesPerSec, bytes, 4);
}

std::wstring FormatVolume(uint64_t b) {
    static const wchar_t* const units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    return Scale(static_cast<double>(b), units, 5);
}

}  // namespace nsm
