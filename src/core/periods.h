#pragma once
#include <cstdint>

#include "core/time_zone.h"
#include "core/types.h"

namespace nsm {

enum class Period { Today, Yesterday, ThisWeek, LastWeek, ThisMonth, LastMonth, Last30Days, ThisYear, AllTime, Custom };

// Resolves a period to a UTC range [begin, end) whose boundaries are local midnights.
// firstDayOfWeek: 0=Sunday..6=Saturday. Custom dates are inclusive.
UtcRange ResolvePeriod(Period p, int64_t nowUtc, const TimeZone& tz, int firstDayOfWeek,
                       LocalDate customFrom = {}, LocalDate customTo = {});

int64_t HourStart(int64_t utcSec);
LocalDate LocalDateOf(int64_t utcSec, const TimeZone& tz);
int FirstDayOfWeekFromLocale();   // Windows regional setting, 0=Sunday..6=Saturday

}  // namespace nsm
