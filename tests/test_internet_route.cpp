#include <algorithm>

#include "doctest.h"
#include "platform/adapters.h"
#include "platform/internet_route.h"

using namespace nsm;

TEST_CASE("FindInternetInterface matches an enumerated adapter when online" * doctest::test_suite("integration")) {
    auto luid = FindInternetInterface();
    if (!luid) {
        MESSAGE("offline; skipped");
        return;
    }
    auto list = EnumerateAdapters();
    CHECK(std::any_of(list.begin(), list.end(), [&](auto& a) { return a.luid.Value == luid->Value; }));
}

TEST_CASE("NetChangeWatcher starts, stops and restarts" * doctest::test_suite("integration")) {
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    REQUIRE(hwnd != nullptr);
    {
        NetChangeWatcher w;
        CHECK(w.Start(hwnd, WM_APP + 1));
        w.Stop();
        w.Stop();  // idempotent
        CHECK(w.Start(hwnd, WM_APP + 1));
        w.Acknowledge();
        w.Stop();
    }  // destructor after Stop is safe
    {
        NetChangeWatcher w;
        CHECK(w.Start(hwnd, WM_APP + 1));
    }  // destructor stops
    DestroyWindow(hwnd);
}
