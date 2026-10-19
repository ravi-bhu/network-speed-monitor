#include "core/periods.h"

#include "core/civil_date.h"

#include <windows.h>

namespace nsm {

namespace {

int64_t Days(const LocalDate& d) { return DaysFromCivil(d.year, d.month, d.day); }

// Local midnight at the start of the given day number, as UTC.
int64_t MidnightUtc(const TimeZone& tz, int64_t dayNumber) {
    const LocalDate d = CivilFromDays(dayNumber);
    return tz.ToUtc(LocalDateTime{d.year, d.month, d.day, 0, 0, 0});
}

int64_t FloorMod(int64_t a, int64_t b) { return ((a % b) + b) % b; }

}  // namespace

int64_t HourStart(int64_t utcSec) { return utcSec - FloorMod(utcSec, 3600); }

LocalDate LocalDateOf(int64_t utcSec, const TimeZone& tz) {
    const LocalDateTime l = tz.ToLocal(utcSec);
    return LocalDate{l.year, l.month, l.day};
}

UtcRange ResolvePeriod(Period p, int64_t nowUtc, const TimeZone& tz, int firstDayOfWeek,
                       LocalDate customFrom, LocalDate customTo) {
    const LocalDate today = LocalDateOf(nowUtc, tz);
    const int64_t t = Days(today);
    auto range = [&](int64_t fromDay, int64_t endDay) {
        return UtcRange{MidnightUtc(tz, fromDay), MidnightUtc(tz, endDay)};
    };

    switch (p) {
    case Period::Today: return range(t, t + 1);
    case Period::Yesterday: return range(t - 1, t);
    case Period::ThisWeek:
    case Period::LastWeek: {
        const int64_t weekday = FloorMod(t + 4, 7);  // 1970-01-01 was a Thursday; 0=Sunday
        const int64_t start = t - FloorMod(weekday - firstDayOfWeek, 7);
        return p == Period::ThisWeek ? range(start, start + 7) : range(start - 7, start);
    }
    case Period::ThisMonth: {
        const int ny = today.month == 12 ? today.year + 1 : today.year;
        const int nm = today.month == 12 ? 1 : today.month + 1;
        return range(DaysFromCivil(today.year, today.month, 1), DaysFromCivil(ny, nm, 1));
    }
    case Period::LastMonth: {
        const int py = today.month == 1 ? today.year - 1 : today.year;
        const int pm = today.month == 1 ? 12 : today.month - 1;
        return range(DaysFromCivil(py, pm, 1), DaysFromCivil(today.year, today.month, 1));
    }
    case Period::Last30Days: return range(t - 29, t + 1);
    case Period::ThisYear: return range(DaysFromCivil(today.year, 1, 1), DaysFromCivil(today.year + 1, 1, 1));
    case Period::AllTime: return UtcRange{0, MidnightUtc(tz, t + 1)};
    case Period::Custom: return range(Days(customFrom), Days(customTo) + 1);
    }
    return {};
}

int FirstDayOfWeekFromLocale() {
    int v = 0;  // 0=Monday..6=Sunday
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER,
                        reinterpret_cast<LPWSTR>(&v), sizeof(v) / sizeof(wchar_t)) == 0)
        return 0;
    if (v < 0 || v > 6) return 0;
    return (v + 1) % 7;  // -> 0=Sunday..6=Saturday
}

}  // namespace nsm
