#include "core/time_zone.h"

#include <windows.h>

namespace nsm {

namespace {

// Howard Hinnant's civil-date algorithms (proleptic Gregorian, days since 1970-01-01).
int64_t DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void CivilFromDays(int64_t z, int& y, int& m, int& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2));
}

int64_t FloorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

SYSTEMTIME ToSystemTime(const LocalDateTime& t) {
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(t.year);
    st.wMonth = static_cast<WORD>(t.month);
    st.wDay = static_cast<WORD>(t.day);
    st.wHour = static_cast<WORD>(t.hour);
    st.wMinute = static_cast<WORD>(t.minute);
    st.wSecond = static_cast<WORD>(t.second);
    return st;
}

LocalDateTime FromSystemTime(const SYSTEMTIME& s) {
    return LocalDateTime{s.wYear, s.wMonth, s.wDay, s.wHour, s.wMinute, s.wSecond};
}

// Unix seconds -> broken-down UTC, without FILETIME (no range limits).
LocalDateTime UtcBreakdown(int64_t sec) {
    const int64_t days = FloorDiv(sec, 86400);
    const int64_t rem = sec - days * 86400;
    LocalDateTime r;
    CivilFromDays(days, r.year, r.month, r.day);
    r.hour = static_cast<int>(rem / 3600);
    r.minute = static_cast<int>(rem % 3600 / 60);
    r.second = static_cast<int>(rem % 60);
    return r;
}

int64_t UtcSeconds(const LocalDateTime& t) {
    return DaysFromCivil(t.year, t.month, t.day) * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
}

}  // namespace

struct TimeZone::Impl { DYNAMIC_TIME_ZONE_INFORMATION dtzi{}; };

TimeZone TimeZone::Local() {
    auto impl = std::make_shared<Impl>();
    GetDynamicTimeZoneInformation(&impl->dtzi);
    return TimeZone(std::move(impl));
}

std::optional<TimeZone> TimeZone::FromKey(const std::wstring& key) {
    DYNAMIC_TIME_ZONE_INFORMATION d{};
    for (DWORD i = 0; EnumDynamicTimeZoneInformation(i, &d) == ERROR_SUCCESS; ++i) {
        if (key == d.TimeZoneKeyName) {
            auto impl = std::make_shared<Impl>();
            impl->dtzi = d;
            return TimeZone(std::move(impl));
        }
    }
    return std::nullopt;
}

LocalDateTime TimeZone::ToLocal(int64_t utcSec) const {
    const LocalDateTime u = UtcBreakdown(utcSec);
    const SYSTEMTIME in = ToSystemTime(u);
    SYSTEMTIME out{};
    if (!SystemTimeToTzSpecificLocalTimeEx(&impl_->dtzi, &in, &out)) return u;
    return FromSystemTime(out);
}

int64_t TimeZone::ToUtc(const LocalDateTime& t) const {
    const int64_t want = UtcSeconds(t);
    const SYSTEMTIME in = ToSystemTime(t);
    SYSTEMTIME out{};
    if (!TzSpecificLocalTimeToSystemTimeEx(&impl_->dtzi, &in, &out)) return want;
    const int64_t api = UtcSeconds(FromSystemTime(out));

    // The API picks one instant for an ambiguous time; scan neighbours for the earliest
    // instant that maps back to the requested wall-clock time. A nonexistent time has
    // no such instant, so the API's own resolution stands.
    auto matches = [&](int64_t cand) { return UtcSeconds(ToLocal(cand)) == want; };
    int64_t best = api;
    bool found = matches(api);
    for (int64_t off : {-7200, -3600, -1800, 1800, 3600, 7200}) {
        const int64_t cand = api + off;
        if (matches(cand) && (!found || cand < best)) { best = cand; found = true; }
    }
    return best;
}

}  // namespace nsm
