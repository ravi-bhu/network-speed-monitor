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

int64_t HourStart(int64_t utcSec);  // the start of the UTC hour
// The start of the local clock hour that contains utcSec, as a UTC instant: the bucket for hourly usage.
// It uses the offset in effect at that instant, so +5:30 and +5:45 zones get buckets on the local hour
// (whole-hour zones: the same as HourStart). When a transition moves the clock by part of an hour (Lord
// Howe's 30-minute DST, Chatham's 2:45 change), the first bucket after it starts at the transition, so a
// bucket's key is always its first instant.
int64_t LocalHourStart(int64_t utcSec, const TimeZone& tz);
LocalDate LocalDateOf(int64_t utcSec, const TimeZone& tz);
int FirstDayOfWeekFromLocale();   // Windows regional setting, 0=Sunday..6=Saturday

}  // namespace nsm
