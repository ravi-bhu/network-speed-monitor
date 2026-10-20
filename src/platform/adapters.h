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
    bool recognizable = true;  // offered in the adapter menu (IsRecognizableAdapter); measured either way
};

// Drops software loopback and transition tunnels (Teredo, ISATAP, 6to4).
bool IsListedInterface(ULONG ifType, TUNNEL_TYPE tunnelType);
// hardwareInterface is nullopt when GetIfEntry2 failed: Wi-Fi and Ethernet then go by type alone,
// so a real NIC is not taken for a virtual one. PPP and tunnel interfaces (VPNs) are virtual.
AdapterKind ClassifyAdapter(ULONG ifType, std::optional<bool> hardwareInterface);
// Whether a user would recognize the adapter in a list. Not a Wi-Fi Direct virtual adapter ("Local
// Area Connection* N") unless it is up (Mobile hotspot), and not one that has been down since boot:
// down with no traffic counted. hadTraffic is true when the counters could not be read.
bool IsRecognizableAdapter(ULONG ifType, std::optional<bool> hardwareInterface, bool up, bool hadTraffic);
// A reported link speed in bit/s; the "unknown" sentinel (ULONG64 max) becomes 0.
uint64_t LinkSpeed(ULONG64 reported);

// Calls call(buf.data(), &size) with a buffer of size bytes, growing it to the size Windows asks
// for while it returns ERROR_BUFFER_OVERFLOW, at most `attempts` times. Returns the last result:
// ERROR_BUFFER_OVERFLOW when the attempts ran out.
template <class Call>
ULONG CallWithGrowingBuffer(std::vector<BYTE>& buf, ULONG size, int attempts, Call&& call) {
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < attempts && rc == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buf.assign(size, 0);
        rc = call(buf.data(), &size);
    }
    return rc;
}

// nullopt when Windows could not list the adapters, so the caller can keep its last list.
std::optional<std::vector<AdapterInfo>> TryEnumerateAdapters();
std::vector<AdapterInfo> EnumerateAdapters();  // empty on failure
std::optional<CounterReading> ReadCounters(NET_LUID luid, double nowSec);
double MonotonicSeconds();

}  // namespace nsm
