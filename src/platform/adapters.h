#pragma once
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <ifdef.h>
#include <iptypes.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/counter_tracker.h"
#include "core/types.h"

namespace nsm {

struct AdapterInfo {
    std::wstring id;  // interface GUID "{...}" from AdapterName
    NET_LUID luid;
    std::wstring name;  // FriendlyName
    std::wstring description;
    AdapterKind kind;
    bool up;
    uint64_t linkBitsPerSec;
};

// Drops software loopback and transition tunnels (Teredo, ISATAP, 6to4).
bool IsListedInterface(ULONG ifType, TUNNEL_TYPE tunnelType);
AdapterKind ClassifyAdapter(ULONG ifType, bool hardwareInterface);
std::vector<AdapterInfo> EnumerateAdapters();
std::optional<CounterReading> ReadCounters(NET_LUID luid, double nowSec);
double MonotonicSeconds();

}  // namespace nsm
