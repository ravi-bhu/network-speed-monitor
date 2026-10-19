#include <set>
#include <string>

#include "doctest.h"
#include "platform/adapters.h"

using namespace nsm;

TEST_CASE("IsListedInterface drops loopback and transition tunnels only") {
    CHECK(!IsListedInterface(IF_TYPE_SOFTWARE_LOOPBACK, TUNNEL_TYPE_NONE));
    CHECK(!IsListedInterface(IF_TYPE_TUNNEL, TUNNEL_TYPE_TEREDO));
    CHECK(!IsListedInterface(IF_TYPE_TUNNEL, TUNNEL_TYPE_ISATAP));
    CHECK(!IsListedInterface(IF_TYPE_TUNNEL, TUNNEL_TYPE_6TO4));
    CHECK(IsListedInterface(IF_TYPE_PROP_VIRTUAL, TUNNEL_TYPE_NONE));  // WireGuard
    CHECK(IsListedInterface(IF_TYPE_ETHERNET_CSMACD, TUNNEL_TYPE_NONE));
}

TEST_CASE("ClassifyAdapter") {
    CHECK(ClassifyAdapter(IF_TYPE_IEEE80211, true) == AdapterKind::WiFi);
    CHECK(ClassifyAdapter(IF_TYPE_IEEE80211, false) == AdapterKind::Virtual);
    CHECK(ClassifyAdapter(IF_TYPE_ETHERNET_CSMACD, true) == AdapterKind::Ethernet);
    CHECK(ClassifyAdapter(IF_TYPE_ETHERNET_CSMACD, false) == AdapterKind::Virtual);
    CHECK(ClassifyAdapter(IF_TYPE_WWANPP, true) == AdapterKind::Other);
}

TEST_CASE("EnumerateAdapters returns unique GUID ids without loopback" * doctest::test_suite("integration")) {
    auto list = EnumerateAdapters();
    REQUIRE(!list.empty());
    std::set<std::wstring> ids;
    for (auto& a : list) {
        CHECK(a.id.front() == L'{');
        CHECK(ids.insert(a.id).second);
    }
}

TEST_CASE("ReadCounters reads a live adapter" * doctest::test_suite("integration")) {
    auto list = EnumerateAdapters();
    REQUIRE(!list.empty());
    CHECK(ReadCounters(list.front().luid, MonotonicSeconds()).has_value());
}
