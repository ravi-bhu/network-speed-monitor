#include "doctest.h"
#include "platform/strings.h"
#include "resource.h"

TEST_CASE("LoadStr reads the STRINGTABLE" * doctest::test_suite("integration")) {
    CHECK(nsm::LoadStr(IDS_APP_NAME) == L"Network Speed Monitor");
    CHECK(nsm::LoadStr(65000).empty());
}

TEST_CASE("Overlay labels come from the STRINGTABLE" * doctest::test_suite("integration")) {
    CHECK(nsm::LoadStr(IDS_LABEL_UP) == L"U:");
    CHECK(nsm::LoadStr(IDS_LABEL_DOWN) == L"D:");
    CHECK(nsm::LoadStr(IDS_NO_VALUE) == L"—");
}
