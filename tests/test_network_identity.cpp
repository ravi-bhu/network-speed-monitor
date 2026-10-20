#include <windows.h>
#include <objbase.h>

#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "platform/adapters.h"
#include "platform/network_identity.h"

using namespace nsm;

namespace {

AdapterNetworkCandidate Candidate(const wchar_t* adapter, const wchar_t* key, bool internet,
                                  const wchar_t* name = L"") {
    return AdapterNetworkCandidate{adapter, NetworkIdentity{key, name}, internet};
}

}  // namespace

TEST_CASE("PickAdapterNetworks keeps a single network per adapter") {
    const auto map = PickAdapterNetworks({Candidate(L"{A}", L"{NET-1}", true, L"Home"),
                                          Candidate(L"{B}", L"{NET-2}", false, L"Office")});
    REQUIRE(map.size() == 2);
    CHECK(map.at(L"{A}").key == L"{NET-1}");
    CHECK(map.at(L"{A}").name == L"Home");
    CHECK(map.at(L"{B}").key == L"{NET-2}");
}

TEST_CASE("PickAdapterNetworks prefers the network with internet, in any order") {
    const auto named = Candidate(L"{A}", L"{NET-9}", true, L"Home");
    const auto unidentified = Candidate(L"{A}", L"{NET-1}", false, L"Unidentified network");
    CHECK(PickAdapterNetworks({named, unidentified}).at(L"{A}").key == L"{NET-9}");
    CHECK(PickAdapterNetworks({unidentified, named}).at(L"{A}").key == L"{NET-9}");
}

TEST_CASE("PickAdapterNetworks breaks ties by the smallest key, in any order") {
    const auto a = Candidate(L"{A}", L"{NET-1}", false);
    const auto b = Candidate(L"{A}", L"{NET-2}", false);
    CHECK(PickAdapterNetworks({a, b}).at(L"{A}").key == L"{NET-1}");
    CHECK(PickAdapterNetworks({b, a}).at(L"{A}").key == L"{NET-1}");
    const auto c = Candidate(L"{A}", L"{NET-3}", true);
    const auto d = Candidate(L"{A}", L"{NET-4}", true);
    CHECK(PickAdapterNetworks({d, b, c, a}).at(L"{A}").key == L"{NET-3}");
    CHECK(PickAdapterNetworks({a, c, b, d}).at(L"{A}").key == L"{NET-3}");
}

TEST_CASE("PickAdapterNetworks ignores candidates without an adapter id or key") {
    const auto map = PickAdapterNetworks({Candidate(L"", L"{NET-1}", true), Candidate(L"{A}", L"", true),
                                          Candidate(L"{A}", L"{NET-5}", false)});
    REQUIRE(map.size() == 1);
    CHECK(map.at(L"{A}").key == L"{NET-5}");
    CHECK(PickAdapterNetworks({}).empty());
}

TEST_CASE("QueryAdapterNetworks maps connected adapters to networks" * doctest::test_suite("integration")) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct Guard {
        bool uninit;  // S_OK or S_FALSE both need a matching CoUninitialize
        ~Guard() {
            if (uninit) CoUninitialize();
        }
    } guard{SUCCEEDED(hr)};  // runs even when a REQUIRE below fails
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
    // NLM also reports adapters that EnumerateAdapters filters out (tunnels), so only some ids
    // need to match; one match shows both use the same id format.
    size_t listed = 0;
    for (auto& [id, net] : map) {
        listed += ids.count(id);
        REQUIRE(!net.key.empty());
        CHECK(net.key.front() == L'{');
        MESSAGE("network name length=", net.name.size());
    }
    if (!map.empty()) CHECK(listed > 0);
    MESSAGE("networks on unlisted adapters=", map.size() - listed);
}
