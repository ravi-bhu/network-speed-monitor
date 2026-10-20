#include "core/time_zone.h"

#include "core/civil_date.h"

#include <windows.h>

namespace nsm {

namespace {

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
    const LocalDate d = CivilFromDays(days);
    r.year = d.year; r.month = d.month; r.day = d.day;
    r.hour = static_cast<int>(rem / 3600);
    r.minute = static_cast<int>(rem % 3600 / 60);
    r.second = static_cast<int>(rem % 60);
    return r;
}

int64_t UtcSeconds(const LocalDateTime& t) {
    return DaysFromCivil(t.year, t.month, t.day) * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
}

// SYSTEMTIME's range for the conversion APIs; outside it a year would also be truncated to a WORD.
bool InSystemTimeRange(const LocalDateTime& t) { return t.year >= 1601 && t.year <= 30827; }

// The zone's standard offset (local = UTC + offset), in seconds: used when the conversion APIs can't be.
int64_t StandardOffset(const DYNAMIC_TIME_ZONE_INFORMATION& z) {
    return -(static_cast<int64_t>(z.Bias) + z.StandardBias) * 60;
}

}  // namespace

struct TimeZone::Impl { DYNAMIC_TIME_ZONE_INFORMATION dtzi{}; };

TimeZone TimeZone::Local() {
    auto impl = std::make_shared<Impl>();
    // On failure the struct's contents are undefined: use UTC (a zeroed zone has no bias and no DST).
    if (GetDynamicTimeZoneInformation(&impl->dtzi) == TIME_ZONE_ID_INVALID) impl->dtzi = {};
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
    if (InSystemTimeRange(u)) {
        const SYSTEMTIME in = ToSystemTime(u);
        SYSTEMTIME out{};
        if (SystemTimeToTzSpecificLocalTimeEx(&impl_->dtzi, &in, &out)) return FromSystemTime(out);
    }
    return UtcBreakdown(utcSec + StandardOffset(impl_->dtzi));
}

int64_t TimeZone::ToUtc(const LocalDateTime& t) const {
    const int64_t want = UtcSeconds(t);
    const int64_t standard = want - StandardOffset(impl_->dtzi);
    if (!InSystemTimeRange(t)) return standard;
    const SYSTEMTIME in = ToSystemTime(t);
    SYSTEMTIME out{};
    if (!TzSpecificLocalTimeToSystemTimeEx(&impl_->dtzi, &in, &out)) return standard;
    const int64_t api = UtcSeconds(FromSystemTime(out));

    // The API picks one instant for an ambiguous time; scan neighbours for the earliest
    // instant that maps back to the requested wall-clock time. A nonexistent time has
    // no such instant and is resolved below.
    auto matches = [&](int64_t cand) { return UtcSeconds(ToLocal(cand)) == want; };
    int64_t best = api;
    bool found = matches(api);
    for (int64_t off : {-7200, -3600, -1800, 1800, 3600, 7200}) {
        const int64_t cand = api + off;
        if (matches(cand) && (!found || cand < best)) { best = cand; found = true; }
    }
    if (!found) {
        // Nonexistent local time (spring-forward gap): the first instant whose local
        // time is at or after the requested one, i.e. the transition instant.
        int64_t lo = api - 7200, hi = api + 7200;  // local(lo) < want <= local(hi)
        while (hi - lo > 1) {
            const int64_t mid = lo + (hi - lo) / 2;
            if (UtcSeconds(ToLocal(mid)) >= want) hi = mid; else lo = mid;
        }
        return hi;
    }
    return best;
}

}  // namespace nsm
