#pragma once
#include <map>
#include <string>
#include <vector>

namespace nsm {

struct NetworkIdentity {
    std::wstring key;   // NLM network GUID "{...}"
    std::wstring name;  // may be empty
};

// One connected network that the Network List Manager reports for an adapter.
struct AdapterNetworkCandidate {
    std::wstring adapterId;
    NetworkIdentity network;
    bool internet = false;  // the connection has IPv4 or IPv6 internet connectivity
};

// Picks one network per adapter, whatever the enumeration order: a network with internet
// connectivity first, then the smallest key. Candidates without an adapter id or key are ignored.
std::map<std::wstring, NetworkIdentity> PickAdapterNetworks(const std::vector<AdapterNetworkCandidate>& candidates);

// Maps adapter id ("{GUID}", same format as AdapterInfo::id) to the network it is
// connected to, via the Network List Manager; PickAdapterNetworks chooses when an adapter
// has several. The caller must have COM initialized (STA). Never throws; on any COM failure
// returns what was collected so far.
std::map<std::wstring, NetworkIdentity> QueryAdapterNetworks();

}  // namespace nsm
