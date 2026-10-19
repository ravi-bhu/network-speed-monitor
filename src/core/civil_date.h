#pragma once
#include <cstdint>

#include "core/types.h"

namespace nsm {

// Proleptic Gregorian calendar <-> day number (day 0 = 1970-01-01).
int64_t DaysFromCivil(int year, int month, int day);
LocalDate CivilFromDays(int64_t dayNumber);

}  // namespace nsm
