#include <windows.h>
#include <objbase.h>

#include <set>
#include <string>

#include "doctest.h"
#include "platform/adapters.h"
#include "platform/network_identity.h"

using namespace nsm;

TEST_CASE("QueryAdapterNetworks maps connected adapters to networks" * doctest::test_suite("integration")) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool uninit = SUCCEEDED(hr);  // S_OK or S_FALSE both need a matching CoUninitialize
    REQUIRE((SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE));

    auto adapters = EnumerateAdapters();
    std::set<std::wstring> ids;
    bool anyUp = false;
    for (auto& a : adapters) {
        ids.insert(a.id);
        anyUp = anyUp || a.up;
    }

    auto map = QueryAdapterNetworks();
    if (anyUp) CHECK(!map.empty());
    for (auto& [id, net] : map) {
        CHECK(ids.count(id) == 1);
        REQUIRE(!net.key.empty());
        CHECK(net.key.front() == L'{');
        MESSAGE("network name length=", net.name.size());
    }

    if (uninit) CoUninitialize();
}
