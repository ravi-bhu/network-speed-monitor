#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>

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
    CHECK(ClassifyAdapter(IF_TYPE_PROP_VIRTUAL, true) == AdapterKind::Virtual);  // WireGuard
    CHECK(ClassifyAdapter(IF_TYPE_PROP_VIRTUAL, false) == AdapterKind::Virtual);
    CHECK(ClassifyAdapter(IF_TYPE_WWANPP, true) == AdapterKind::Other);
}

TEST_CASE("ClassifyAdapter: PPP and tunnel VPNs are virtual") {
    CHECK(ClassifyAdapter(IF_TYPE_PPP, false) == AdapterKind::Virtual);     // PPTP, SSTP, L2TP, IKEv2, PPPoE
    CHECK(ClassifyAdapter(IF_TYPE_TUNNEL, false) == AdapterKind::Virtual);  // IP-in-IP, GRE
    CHECK(ClassifyAdapter(IF_TYPE_PPP, std::nullopt) == AdapterKind::Virtual);
}

TEST_CASE("ClassifyAdapter: an unknown hardware flag trusts the interface type") {
    // GetIfEntry2 failed: a real NIC must not turn into Virtual.
    CHECK(ClassifyAdapter(IF_TYPE_IEEE80211, std::nullopt) == AdapterKind::WiFi);
    CHECK(ClassifyAdapter(IF_TYPE_ETHERNET_CSMACD, std::nullopt) == AdapterKind::Ethernet);
    CHECK(ClassifyAdapter(IF_TYPE_PROP_VIRTUAL, std::nullopt) == AdapterKind::Virtual);
    CHECK(ClassifyAdapter(IF_TYPE_WWANPP, std::nullopt) == AdapterKind::Other);
}

TEST_CASE("IsRecognizableAdapter hides Wi-Fi Direct adapters unless up, and adapters idle since boot") {
    // Wi-Fi Direct virtual adapter ("Local Area Connection* N"): only while up, as for Mobile hotspot.
    CHECK_FALSE(IsRecognizableAdapter(IF_TYPE_IEEE80211, false, false, false));
    CHECK_FALSE(IsRecognizableAdapter(IF_TYPE_IEEE80211, false, false, true));
    CHECK(IsRecognizableAdapter(IF_TYPE_IEEE80211, false, true, true));
    // Down with no traffic since boot (unused NIC port, Bluetooth PAN): hidden.
    CHECK_FALSE(IsRecognizableAdapter(IF_TYPE_ETHERNET_CSMACD, true, false, false));
    CHECK_FALSE(IsRecognizableAdapter(IF_TYPE_PPP, false, false, false));
    // Down after carrying traffic (cable pulled, VPN disconnected): still listed.
    CHECK(IsRecognizableAdapter(IF_TYPE_ETHERNET_CSMACD, true, false, true));
    CHECK(IsRecognizableAdapter(IF_TYPE_PROP_VIRTUAL, false, false, true));
    // Up: listed.
    CHECK(IsRecognizableAdapter(IF_TYPE_IEEE80211, true, true, false));
    CHECK(IsRecognizableAdapter(IF_TYPE_ETHERNET_CSMACD, false, true, false));  // Hyper-V vEthernet
    // Hardware flag unknown: a Wi-Fi adapter is not taken for Wi-Fi Direct.
    CHECK(IsRecognizableAdapter(IF_TYPE_IEEE80211, std::nullopt, false, true));
}

TEST_CASE("LinkSpeed maps the ULONG64 max \"unknown\" sentinel to 0") {
    constexpr ULONG64 kMax = (std::numeric_limits<ULONG64>::max)();
    CHECK(LinkSpeed(kMax) == 0ull);
    CHECK(LinkSpeed(kMax - 1) == kMax - 1);
    CHECK(LinkSpeed(0ull) == 0ull);
    CHECK(LinkSpeed(1'000'000'000ull) == 1'000'000'000ull);
}

TEST_CASE("CallWithGrowingBuffer retries on overflow and returns the overflow once out of attempts") {
    std::vector<BYTE> buf;
    int calls = 0;
    // Windows asks for more room once, then succeeds.
    ULONG rc = CallWithGrowingBuffer(buf, 16, 5, [&](BYTE* data, ULONG* size) -> ULONG {
        ++calls;
        CHECK(data == buf.data());
        CHECK(*size == buf.size());
        if (*size < 64) {
            *size = 64;
            return ERROR_BUFFER_OVERFLOW;
        }
        return NO_ERROR;
    });
    CHECK(rc == NO_ERROR);
    CHECK(calls == 2);
    CHECK(buf.size() == 64);

    // The list keeps growing faster than we retry: the overflow comes back instead of an empty success.
    calls = 0;
    rc = CallWithGrowingBuffer(buf, 16, 5, [&](BYTE*, ULONG* size) -> ULONG {
        ++calls;
        *size += 16;
        return ERROR_BUFFER_OVERFLOW;
    });
    CHECK(rc == ERROR_BUFFER_OVERFLOW);
    CHECK(calls == 5);

    // Any other error stops at once.
    calls = 0;
    rc = CallWithGrowingBuffer(buf, 16, 5, [&](BYTE*, ULONG*) -> ULONG {
        ++calls;
        return ERROR_NOT_ENOUGH_MEMORY;
    });
    CHECK(rc == ERROR_NOT_ENOUGH_MEMORY);
    CHECK(calls == 1);
}

TEST_CASE("EnumerateAdapters returns unique GUID ids without loopback" * doctest::test_suite("integration")) {
    auto list = EnumerateAdapters();
    REQUIRE(!list.empty());
    std::set<std::wstring> ids;
    for (auto& a : list) {
        CHECK(a.id.front() == L'{');
        CHECK(ids.insert(a.id).second);
        const ULONG ifType = static_cast<ULONG>(a.luid.Info.IfType);  // a bit-field: CHECK can't bind it
        CHECK(ifType != IF_TYPE_SOFTWARE_LOOPBACK);
    }
}

TEST_CASE("TryEnumerateAdapters succeeds on a working system" * doctest::test_suite("integration")) {
    const auto list = TryEnumerateAdapters();
    REQUIRE(list.has_value());
    CHECK(!list->empty());
}

TEST_CASE("ReadCounters reads a live adapter" * doctest::test_suite("integration")) {
    auto list = EnumerateAdapters();
    REQUIRE(!list.empty());
    CHECK(ReadCounters(list.front().luid, MonotonicSeconds()).has_value());
}
