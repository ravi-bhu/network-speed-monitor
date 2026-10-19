#include "doctest.h"

#include "core/civil_date.h"

using namespace nsm;

TEST_CASE("Civil date: day 0 is 1970-01-01") {
    CHECK(DaysFromCivil(1970, 1, 1) == 0);
    CHECK(CivilFromDays(0) == LocalDate{1970, 1, 1});
}
TEST_CASE("Civil date: leap day and month end") {
    CHECK(CivilFromDays(DaysFromCivil(2028, 2, 29)) == LocalDate{2028, 2, 29});
    CHECK(DaysFromCivil(2028, 3, 1) - DaysFromCivil(2028, 2, 29) == 1);
    CHECK(CivilFromDays(DaysFromCivil(2026, 1, 31) + 1) == LocalDate{2026, 2, 1});
    CHECK(CivilFromDays(DaysFromCivil(2026, 12, 31) + 1) == LocalDate{2027, 1, 1});
    CHECK(CivilFromDays(-1) == LocalDate{1969, 12, 31});
}
