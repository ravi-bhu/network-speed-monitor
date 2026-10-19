#pragma once
#include <map>
#include <string>

namespace nsm {

struct NetworkIdentity {
    std::wstring key;   // NLM network GUID "{...}"
    std::wstring name;  // may be empty
};

// Maps adapter id ("{GUID}", same format as AdapterInfo::id) to the network it is
// connected to, via the Network List Manager. The caller must have COM initialized
// (STA). Never throws; on any COM failure returns what was collected so far.
std::map<std::wstring, NetworkIdentity> QueryAdapterNetworks();

}  // namespace nsm
