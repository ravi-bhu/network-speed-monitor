#include "platform/network_identity.h"

#include <windows.h>
#include <netlistmgr.h>
#include <objbase.h>
#include <wrl/client.h>

#include <tuple>

namespace nsm {

using Microsoft::WRL::ComPtr;

namespace {

std::wstring GuidToString(const GUID& g) {
    wchar_t buf[64] = {};
    int n = StringFromGUID2(g, buf, 64);  // uppercase "{...}", same as AdapterInfo::id
    return n > 1 ? std::wstring(buf, static_cast<size_t>(n - 1)) : std::wstring();
}

// True when `a` should be kept over `b` for the same adapter. The name only breaks a tie
// between two connections to the same network, so the result never depends on order.
bool Preferred(const AdapterNetworkCandidate& a, const AdapterNetworkCandidate& b) {
    return std::tie(b.internet, a.network.key, a.network.name) < std::tie(a.internet, b.network.key, b.network.name);
}

}  // namespace

std::map<std::wstring, NetworkIdentity> PickAdapterNetworks(const std::vector<AdapterNetworkCandidate>& candidates) {
    std::map<std::wstring, const AdapterNetworkCandidate*> best;
    for (const auto& c : candidates) {
        if (c.adapterId.empty() || c.network.key.empty()) continue;
        auto [it, inserted] = best.try_emplace(c.adapterId, &c);
        if (!inserted && Preferred(c, *it->second)) it->second = &c;
    }
    std::map<std::wstring, NetworkIdentity> result;
    for (const auto& [adapterId, c] : best) result.emplace(adapterId, c->network);
    return result;
}

std::map<std::wstring, NetworkIdentity> QueryAdapterNetworks() {
    // Collect every connected network first: an adapter can have several, and the
    // enumeration order is undocumented, so the choice is made by PickAdapterNetworks.
    std::vector<AdapterNetworkCandidate> candidates;

    ComPtr<INetworkListManager> mgr;
    if (FAILED(CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&mgr)))) return {};

    ComPtr<IEnumNetworkConnections> en;
    if (FAILED(mgr->GetNetworkConnections(&en)) || !en) return {};

    for (;;) {
        ComPtr<INetworkConnection> conn;
        ULONG fetched = 0;
        if (en->Next(1, &conn, &fetched) != S_OK || fetched == 0) break;

        VARIANT_BOOL connected = VARIANT_FALSE;
        if (FAILED(conn->get_IsConnected(&connected)) || connected == VARIANT_FALSE) continue;

        GUID adapterGuid{};
        if (FAILED(conn->GetAdapterId(&adapterGuid))) continue;
        AdapterNetworkCandidate c;
        c.adapterId = GuidToString(adapterGuid);
        if (c.adapterId.empty()) continue;

        ComPtr<INetwork> net;
        if (FAILED(conn->GetNetwork(&net)) || !net) continue;

        GUID netGuid{};
        if (FAILED(net->GetNetworkId(&netGuid))) continue;

        c.network.key = GuidToString(netGuid);
        if (c.network.key.empty()) continue;
        BSTR name = nullptr;
        if (SUCCEEDED(net->GetName(&name)) && name) {
            c.network.name.assign(name, SysStringLen(name));
            SysFreeString(name);
        }
        NLM_CONNECTIVITY connectivity = NLM_CONNECTIVITY_DISCONNECTED;
        if (SUCCEEDED(conn->GetConnectivity(&connectivity)))
            c.internet = (connectivity & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET)) != 0;
        candidates.push_back(std::move(c));
    }
    return PickAdapterNetworks(candidates);
}

}  // namespace nsm
