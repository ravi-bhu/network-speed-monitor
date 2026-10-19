#include "platform/network_identity.h"

#include <windows.h>
#include <netlistmgr.h>
#include <objbase.h>
#include <wrl/client.h>

namespace nsm {

using Microsoft::WRL::ComPtr;

namespace {

std::wstring GuidToString(const GUID& g) {
    wchar_t buf[64] = {};
    int n = StringFromGUID2(g, buf, 64);  // uppercase "{...}", same as AdapterInfo::id
    return n > 1 ? std::wstring(buf, static_cast<size_t>(n - 1)) : std::wstring();
}

}  // namespace

std::map<std::wstring, NetworkIdentity> QueryAdapterNetworks() {
    std::map<std::wstring, NetworkIdentity> result;

    ComPtr<INetworkListManager> mgr;
    if (FAILED(CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&mgr)))) return result;

    ComPtr<IEnumNetworkConnections> en;
    if (FAILED(mgr->GetNetworkConnections(&en)) || !en) return result;

    for (;;) {
        ComPtr<INetworkConnection> conn;
        ULONG fetched = 0;
        if (en->Next(1, &conn, &fetched) != S_OK || fetched == 0) break;

        VARIANT_BOOL connected = VARIANT_FALSE;
        if (FAILED(conn->get_IsConnected(&connected)) || connected == VARIANT_FALSE) continue;

        GUID adapterGuid{};
        if (FAILED(conn->GetAdapterId(&adapterGuid))) continue;
        std::wstring adapterId = GuidToString(adapterGuid);
        if (adapterId.empty() || result.count(adapterId)) continue;  // keep the first network

        ComPtr<INetwork> net;
        if (FAILED(conn->GetNetwork(&net)) || !net) continue;

        GUID netGuid{};
        if (FAILED(net->GetNetworkId(&netGuid))) continue;

        NetworkIdentity ident;
        ident.key = GuidToString(netGuid);
        if (ident.key.empty()) continue;
        BSTR name = nullptr;
        if (SUCCEEDED(net->GetName(&name)) && name) {
            ident.name.assign(name, SysStringLen(name));
            SysFreeString(name);
        }
        result.emplace(std::move(adapterId), std::move(ident));
    }
    return result;
}

}  // namespace nsm
