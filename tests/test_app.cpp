#include "doctest.h"

#include <ctime>

#include "ui/app.h"

using namespace nsm;

namespace {

AdapterInfo Present(const std::wstring& id, const std::wstring& name, uint64_t luid = 0) {
    AdapterInfo a{};
    a.id = id;
    a.luid.Value = luid;
    a.name = name;
    a.kind = AdapterKind::Other;
    a.up = true;
    return a;
}

AdapterMeta Stored(const std::wstring& id, const std::wstring& name, const std::wstring& friendly = L"",
                   bool hidden = false) {
    AdapterMeta m;
    m.id = id;
    m.name = name;
    m.friendlyName = friendly;
    m.hidden = hidden;
    return m;
}

int64_t UtcOf(int y, int mo, int d, int h, int mi = 0) {
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi;
    return _mkgmtime(&tm);
}

}  // namespace

TEST_CASE("DisplayAdapterId: Automatic follows the internet adapter, a pinned adapter stays pinned") {
    CHECK(DisplayAdapterId(L"", L"{WIFI}") == L"{WIFI}");
    CHECK(DisplayAdapterId(L"", L"") == L"");
    CHECK(DisplayAdapterId(L"{VPN}", L"{WIFI}") == L"{VPN}");
    CHECK(DisplayAdapterId(L"{VPN}", L"") == L"{VPN}");  // pinned and gone: shows a dash, no fallback
}

TEST_CASE("AdapterIdForLuid maps the internet interface to a listed adapter") {
    const std::vector<AdapterInfo> list{Present(L"{A}", L"Ethernet", 11), Present(L"{B}", L"Wi-Fi", 22)};
    NET_LUID luid{};
    luid.Value = 22;
    CHECK(AdapterIdForLuid(list, luid) == L"{B}");
    luid.Value = 33;
    CHECK(AdapterIdForLuid(list, luid) == L"");  // e.g. a filtered interface
    CHECK(AdapterIdForLuid(list, std::nullopt) == L"");
}

TEST_CASE("NetworkKeyOf returns the adapter's network, or none") {
    std::map<std::wstring, NetworkIdentity> nets;
    nets[L"{B}"] = NetworkIdentity{L"{NET-1}", L"Home"};
    CHECK(NetworkKeyOf(nets, L"{B}") == L"{NET-1}");
    CHECK(NetworkKeyOf(nets, L"{A}") == L"");
    CHECK(NetworkKeyOf(nets, L"") == L"");
}

TEST_CASE("DisplayNameOf prefers the user's name, then the Windows name") {
    const std::vector<AdapterInfo> present{Present(L"{A}", L"Ethernet 2")};
    const std::vector<AdapterMeta> stored{Stored(L"{A}", L"Ethernet", L"Desk dock"), Stored(L"{B}", L"Wi-Fi"),
                                          Stored(L"{C}", L"Old", L"")};
    CHECK(DisplayNameOf(L"{A}", present, stored) == L"Desk dock");
    CHECK(DisplayNameOf(L"{B}", present, stored) == L"Wi-Fi");  // absent: the stored name
    CHECK(DisplayNameOf(L"{X}", present, stored) == L"{X}");    // unknown: the id
    const std::vector<AdapterMeta> none;
    CHECK(DisplayNameOf(L"{A}", present, none) == L"Ethernet 2");  // store unavailable
    const std::vector<AdapterMeta> renamedAway{Stored(L"{A}", L"Old name")};
    CHECK(DisplayNameOf(L"{A}", present, renamedAway) == L"Ethernet 2");  // live Windows name wins over stored
}

TEST_CASE("BuildAdapterMenu: Automatic first, then visible adapters by display name") {
    const std::vector<AdapterInfo> present{Present(L"{W}", L"Wi-Fi"), Present(L"{E}", L"Ethernet"),
                                           Present(L"{H}", L"vEthernet (WSL)"), Present(L"{V}", L"vpn")};
    const std::vector<AdapterMeta> stored{Stored(L"{H}", L"vEthernet (WSL)", L"", true),
                                          Stored(L"{W}", L"Wi-Fi", L"Attic Wi-Fi")};
    const auto menu = BuildAdapterMenu(present, stored, L"", L"Automatic");
    REQUIRE(menu.size() == 4);
    CHECK(menu[0].id == L"");
    CHECK(menu[0].label == L"Automatic");
    CHECK(menu[0].checked);
    CHECK(menu[1].label == L"Attic Wi-Fi");
    CHECK(menu[2].label == L"Ethernet");
    CHECK(menu[3].label == L"vpn");  // case-insensitive order
    for (size_t i = 1; i < menu.size(); ++i) CHECK_FALSE(menu[i].checked);
}

TEST_CASE("BuildAdapterMenu checks the pinned adapter") {
    const std::vector<AdapterInfo> present{Present(L"{W}", L"Wi-Fi"), Present(L"{E}", L"Ethernet")};
    const auto menu = BuildAdapterMenu(present, {}, L"{W}", L"Automatic");
    REQUIRE(menu.size() == 3);
    CHECK_FALSE(menu[0].checked);
    CHECK_FALSE(menu[1].checked);
    CHECK(menu[2].id == L"{W}");
    CHECK(menu[2].checked);
}

TEST_CASE("BuildAdapterMenu keeps a pinned adapter that is absent or hidden, so the choice stays visible") {
    const std::vector<AdapterInfo> present{Present(L"{E}", L"Ethernet"), Present(L"{H}", L"Hidden one")};
    const std::vector<AdapterMeta> stored{Stored(L"{V}", L"Work VPN"), Stored(L"{H}", L"Hidden one", L"", true)};

    auto menu = BuildAdapterMenu(present, stored, L"{V}", L"Automatic");
    REQUIRE(menu.size() == 3);
    CHECK(menu[2].id == L"{V}");
    CHECK(menu[2].label == L"Work VPN");
    CHECK(menu[2].checked);

    menu = BuildAdapterMenu(present, stored, L"{H}", L"Automatic");
    REQUIRE(menu.size() == 3);
    CHECK(menu[2].id == L"{H}");
    CHECK(menu[2].checked);
}

TEST_CASE("EscapeMenuText doubles ampersands so names are not read as mnemonics") {
    CHECK(EscapeMenuText(L"R&D LAN") == L"R&&D LAN");
    CHECK(EscapeMenuText(L"&&") == L"&&&&");
    CHECK(EscapeMenuText(L"Wi-Fi") == L"Wi-Fi");
}

TEST_CASE("TooltipText: Automatic names the internet adapter, pinned shows the adapter's name") {
    const std::vector<AdapterInfo> present{Present(L"{W}", L"Wi-Fi")};
    const std::vector<AdapterMeta> stored{Stored(L"{V}", L"Work VPN", L"VPN")};
    CHECK(TooltipText(L"", L"{W}", present, stored, L"Automatic: %s", L"Automatic: no connection") ==
          L"Automatic: Wi-Fi");
    CHECK(TooltipText(L"", L"", present, stored, L"Automatic: %s", L"Automatic: no connection") ==
          L"Automatic: no connection");
    CHECK(TooltipText(L"{V}", L"{W}", present, stored, L"Automatic: %s", L"x") == L"VPN");
    CHECK(TooltipText(L"", L"{W}", present, stored, L"%s (automatic)", L"x") == L"Wi-Fi (automatic)");
}

TEST_CASE("OtherInstanceRunning: an existing mutex, or one we may not open, means another copy runs") {
    CHECK_FALSE(OtherInstanceRunning(true, ERROR_SUCCESS));        // we created it: first instance
    CHECK(OtherInstanceRunning(true, ERROR_ALREADY_EXISTS));       // opened the running copy's mutex
    CHECK(OtherInstanceRunning(false, ERROR_ACCESS_DENIED));       // the running copy is elevated
    CHECK_FALSE(OtherInstanceRunning(false, ERROR_INVALID_HANDLE)); // any other failure: run without the guard
}

TEST_CASE("ShouldRetryOpen: only while nothing is recorded and there is a data folder") {
    CHECK(ShouldRetryOpen(false, true));
    CHECK_FALSE(ShouldRetryOpen(true, true));
    CHECK_FALSE(ShouldRetryOpen(false, false));
}

TEST_CASE("RollUpDue once every 24 h of uptime") {
    const uint64_t day = 24ull * 3600 * 1000;
    CHECK_FALSE(RollUpDue(1000, 1000));
    CHECK_FALSE(RollUpDue(1000, 1000 + day - 1));
    CHECK(RollUpDue(1000, 1000 + day));
    CHECK(RollUpDue(1000, 1000 + 3 * day));
}

TEST_CASE("FlushAccumulator writes everything and empties the accumulator") {
    UsageAccumulator acc;
    acc.Add(UtcOf(2026, 10, 18, 10), Series::Adapter, L"{A}", L"{N}", 100, 10);
    acc.Add(UtcOf(2026, 10, 18, 10), Series::Internet, L"{A}", L"{N}", 100, 10);
    std::vector<UsageRow> written;
    const bool ok = FlushAccumulator(acc, [&](std::span<const UsageRow> rows) {
        written.assign(rows.begin(), rows.end());
        return true;
    });
    CHECK(ok);
    CHECK(written.size() == 2);
    CHECK(acc.Empty());
}

TEST_CASE("FlushAccumulator puts rows back when the write fails, so the next flush retries without loss") {
    UsageAccumulator acc;
    const int64_t h10 = UtcOf(2026, 10, 18, 10);
    acc.Add(h10, Series::Adapter, L"{A}", L"{N}", 100, 10);
    acc.Add(h10, Series::Internet, L"{A}", L"", 7, 3);

    CHECK_FALSE(FlushAccumulator(acc, [](std::span<const UsageRow>) { return false; }));
    CHECK_FALSE(acc.Empty());

    // More traffic in the same hour arrives before the retry; it is summed, not duplicated.
    acc.Add(h10, Series::Adapter, L"{A}", L"{N}", 1, 1);

    std::vector<UsageRow> written;
    CHECK(FlushAccumulator(acc, [&](std::span<const UsageRow> rows) {
        written.assign(rows.begin(), rows.end());
        return true;
    }));
    REQUIRE(written.size() == 2);
    uint64_t rx = 0, tx = 0;
    for (const auto& r : written) {
        CHECK(r.hourUtc == h10);
        rx += r.rx;
        tx += r.tx;
        if (r.series == Series::Adapter) {
            CHECK(r.networkKey == L"{N}");
            CHECK(r.rx == 101);
            CHECK(r.tx == 11);
        } else {
            CHECK(r.networkKey == L"");
            CHECK(r.rx == 7);
            CHECK(r.tx == 3);
        }
    }
    CHECK(rx == 108);
    CHECK(tx == 14);
    CHECK(acc.Empty());
}

TEST_CASE("FlushAccumulator with nothing to write does not call the writer") {
    UsageAccumulator acc;
    bool called = false;
    CHECK(FlushAccumulator(acc, [&](std::span<const UsageRow>) {
        called = true;
        return true;
    }));
    CHECK_FALSE(called);
}

TEST_CASE("ArgValue finds a flag's value on the command line") {
    CHECK(ArgValue(L"NetworkSpeedMonitor.exe --trace C:\\t.csv", L"--trace") == L"C:\\t.csv");
    CHECK(ArgValue(L"\"C:\\a b\\NetworkSpeedMonitor.exe\" --trace \"C:\\x y\\t.csv\"", L"--trace") ==
          L"C:\\x y\\t.csv");
    CHECK(ArgValue(L"NetworkSpeedMonitor.exe --trace", L"--trace") == L"");
    CHECK(ArgValue(L"NetworkSpeedMonitor.exe", L"--trace") == L"");
}

TEST_CASE("RecordTick: each adapter row carries its own network, the internet row the internet adapter's") {
    std::map<std::wstring, NetworkIdentity> nets;
    nets[L"{W}"] = NetworkIdentity{L"{HOME}", L"Home"};
    nets[L"{V}"] = NetworkIdentity{L"{CORP}", L"Corp"};
    TickResult t;
    t.perAdapter = {UsageDelta{L"{W}", 100, 10}, UsageDelta{L"{V}", 40, 4}, UsageDelta{L"{E}", 7, 1}};
    t.internet = UsageDelta{L"{V}", 40, 4};  // the internet goes through the VPN

    UsageAccumulator acc;
    const int64_t h10 = UtcOf(2026, 10, 18, 10);
    RecordTick(acc, h10, t, nets);
    const std::vector<UsageRow> rows = acc.Take();
    REQUIRE(rows.size() == 4);
    int adapterRows = 0, internetRows = 0;
    for (const auto& r : rows) {
        CHECK(r.hourUtc == h10);
        if (r.series == Series::Internet) {
            ++internetRows;
            CHECK(r.adapterId == L"{V}");
            CHECK(r.networkKey == L"{CORP}");
            CHECK(r.rx == 40);
            CHECK(r.tx == 4);
            continue;
        }
        ++adapterRows;
        if (r.adapterId == L"{W}") {
            CHECK(r.networkKey == L"{HOME}");
            CHECK(r.rx == 100);
            CHECK(r.tx == 10);
        } else if (r.adapterId == L"{V}") {
            CHECK(r.networkKey == L"{CORP}");
        } else {
            CHECK(r.adapterId == L"{E}");
            CHECK(r.networkKey == L"");  // no known network
        }
    }
    CHECK(adapterRows == 3);
    CHECK(internetRows == 1);
}

TEST_CASE("RecordTick without internet traffic writes adapter rows only") {
    TickResult t;
    t.perAdapter = {UsageDelta{L"{W}", 5, 5}};
    UsageAccumulator acc;
    RecordTick(acc, UtcOf(2026, 10, 18, 10), t, {});
    const std::vector<UsageRow> rows = acc.Take();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].series == Series::Adapter);
    CHECK(rows[0].networkKey == L"");

    RecordTick(acc, UtcOf(2026, 10, 18, 10), TickResult{}, {});
    CHECK(acc.Empty());
}

TEST_CASE("AdaptersToSave: present adapters whose stored name or kind differs, or that are not stored") {
    std::vector<AdapterInfo> present{Present(L"{A}", L"Ethernet"), Present(L"{B}", L"Wi-Fi"), Present(L"{C}", L"New")};
    present[1].kind = AdapterKind::WiFi;
    std::vector<AdapterMeta> stored{Stored(L"{A}", L"Ethernet", L"Desk"), Stored(L"{B}", L"Wi-Fi")};
    stored[1].kind = AdapterKind::Ethernet;

    const std::vector<AdapterInfo> out = AdaptersToSave(present, stored);
    REQUIRE(out.size() == 2);
    CHECK(out[0].id == L"{B}");  // kind changed
    CHECK(out[1].id == L"{C}");  // not stored yet

    stored[1].kind = AdapterKind::WiFi;
    stored.push_back(Stored(L"{C}", L"New"));
    CHECK(AdaptersToSave(present, stored).empty());
}

TEST_CASE("AdaptersToSave offers a write that did not land again, since only the store's rows count as saved") {
    const std::vector<AdapterInfo> present{Present(L"{A}", L"Ethernet")};
    // The upsert failed (e.g. the database was locked), so reading the adapters back still lacks it.
    const std::vector<AdapterMeta> afterFailedWrite;
    CHECK(AdaptersToSave(present, afterFailedWrite).size() == 1);
    CHECK(AdaptersToSave(present, afterFailedWrite).size() == 1);  // and again at the next refresh
}

TEST_CASE("NetworksToSave: named networks not stored or renamed, each key once") {
    std::map<std::wstring, NetworkIdentity> nets;
    nets[L"{A}"] = NetworkIdentity{L"{HOME}", L"Home"};
    nets[L"{B}"] = NetworkIdentity{L"{HOME}", L"Home"};  // two adapters on one network
    nets[L"{C}"] = NetworkIdentity{L"{CAFE}", L"Cafe 2"};
    nets[L"{D}"] = NetworkIdentity{L"", L"Unidentified"};  // no key: nothing to save
    const std::vector<NetworkMeta> stored{NetworkMeta{L"{CAFE}", L"Cafe"}};

    const std::vector<NetworkMeta> out = NetworksToSave(nets, stored);
    REQUIRE(out.size() == 2);
    CHECK(out[0].key == L"{HOME}");
    CHECK(out[0].name == L"Home");
    CHECK(out[1].key == L"{CAFE}");
    CHECK(out[1].name == L"Cafe 2");

    const std::vector<NetworkMeta> saved{NetworkMeta{L"{CAFE}", L"Cafe 2"}, NetworkMeta{L"{HOME}", L"Home"}};
    CHECK(NetworksToSave(nets, saved).empty());
    CHECK(NetworksToSave(nets, {}).size() == 2);  // a failed write is not in the store: offered again
}

TEST_CASE("FollowDisplayAdapter clears the graph when the shown adapter changes") {
    SpeedHistory history;
    std::wstring shown;
    CHECK(FollowDisplayAdapter(shown, L"{W}", history));  // first tick: nothing to keep
    CHECK(shown == L"{W}");
    history.Push(Speed{1000, 100});
    history.Push(Speed{2000, 200});

    CHECK_FALSE(FollowDisplayAdapter(shown, L"{W}", history));
    CHECK(history.Size() == 2);

    CHECK(FollowDisplayAdapter(shown, L"{E}", history));  // pinned another adapter
    CHECK(shown == L"{E}");
    CHECK(history.Size() == 0);

    history.Push(Speed{1, 1});
    CHECK(FollowDisplayAdapter(shown, L"", history));  // Automatic with no internet adapter
    CHECK(history.Size() == 0);
}
