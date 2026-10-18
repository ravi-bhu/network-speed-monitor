#include "doctest.h"
#include "platform/strings.h"
#include "resource.h"

TEST_CASE("LoadStr reads the STRINGTABLE" * doctest::test_suite("integration")) {
    CHECK(nsm::LoadStr(IDS_APP_NAME) == L"Network Speed Monitor");
    CHECK(nsm::LoadStr(65000).empty());
}
