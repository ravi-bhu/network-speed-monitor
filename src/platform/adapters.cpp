#include "platform/adapters.h"

#include <algorithm>
#include <limits>

namespace nsm {

namespace {
uint64_t LinkSpeed(ULONG64 v) { return v == (std::numeric_limits<ULONG64>::max)() ? 0 : v; }
}  // namespace

bool IsListedInterface(ULONG ifType, TUNNEL_TYPE tunnelType) {
    if (ifType == IF_TYPE_SOFTWARE_LOOPBACK) return false;
    if (ifType == IF_TYPE_TUNNEL &&
        (tunnelType == TUNNEL_TYPE_TEREDO || tunnelType == TUNNEL_TYPE_ISATAP ||
         tunnelType == TUNNEL_TYPE_6TO4))
        return false;
    return true;
}

AdapterKind ClassifyAdapter(ULONG ifType, bool hardwareInterface) {
    if (ifType == IF_TYPE_IEEE80211) return hardwareInterface ? AdapterKind::WiFi : AdapterKind::Virtual;
    if (ifType == IF_TYPE_ETHERNET_CSMACD)
        return hardwareInterface ? AdapterKind::Ethernet : AdapterKind::Virtual;
    if (ifType == IF_TYPE_PROP_VIRTUAL) return AdapterKind::Virtual;
    return AdapterKind::Other;
}

std::vector<AdapterInfo> EnumerateAdapters() {
    std::vector<AdapterInfo> out;
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 16 * 1024;
    std::vector<BYTE> buf;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 5 && rc == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buf.assign(size, 0);
        rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
                                  reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (rc != NO_ERROR) return out;

    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (!a->AdapterName) continue;
        if (!IsListedInterface(a->IfType, a->TunnelType)) continue;

        bool hardware = false;
        MIB_IF_ROW2 row{};
        row.InterfaceLuid = a->Luid;
        if (GetIfEntry2(&row) == NO_ERROR) hardware = row.InterfaceAndOperStatusFlags.HardwareInterface != 0;

        AdapterInfo info{};
        int n = MultiByteToWideChar(CP_ACP, 0, a->AdapterName, -1, nullptr, 0);
        if (n > 1) {
            info.id.resize(static_cast<size_t>(n));
            MultiByteToWideChar(CP_ACP, 0, a->AdapterName, -1, info.id.data(), n);
            info.id.resize(static_cast<size_t>(n - 1));
        }
        if (info.id.empty()) continue;
        info.luid = a->Luid;
        if (a->FriendlyName) info.name = a->FriendlyName;
        if (a->Description) info.description = a->Description;
        info.kind = ClassifyAdapter(a->IfType, hardware);
        info.up = a->OperStatus == IfOperStatusUp;
        info.linkBitsPerSec = (std::max)(LinkSpeed(a->ReceiveLinkSpeed), LinkSpeed(a->TransmitLinkSpeed));
        out.push_back(std::move(info));
    }
    return out;
}

std::optional<CounterReading> ReadCounters(NET_LUID luid, double nowSec) {
    MIB_IF_ROW2 row{};
    row.InterfaceLuid = luid;
    if (GetIfEntry2(&row) != NO_ERROR) return std::nullopt;
    CounterReading r{};
    r.rxBytes = row.InOctets;
    r.txBytes = row.OutOctets;
    r.linkBitsPerSec = (std::max)(LinkSpeed(row.ReceiveLinkSpeed), LinkSpeed(row.TransmitLinkSpeed));
    r.timeSec = nowSec;
    return r;
}

double MonotonicSeconds() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart);
}

}  // namespace nsm
