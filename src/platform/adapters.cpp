#include "platform/adapters.h"

#include <algorithm>
#include <limits>

namespace nsm {

uint64_t LinkSpeed(ULONG64 reported) { return reported == (std::numeric_limits<ULONG64>::max)() ? 0 : reported; }

bool IsListedInterface(ULONG ifType, TUNNEL_TYPE tunnelType) {
    if (ifType == IF_TYPE_SOFTWARE_LOOPBACK) return false;
    if (ifType == IF_TYPE_TUNNEL &&
        (tunnelType == TUNNEL_TYPE_TEREDO || tunnelType == TUNNEL_TYPE_ISATAP ||
         tunnelType == TUNNEL_TYPE_6TO4))
        return false;
    return true;
}

AdapterKind ClassifyAdapter(ULONG ifType, std::optional<bool> hardwareInterface) {
    const bool hardware = hardwareInterface.value_or(true);
    if (ifType == IF_TYPE_IEEE80211) return hardware ? AdapterKind::WiFi : AdapterKind::Virtual;
    if (ifType == IF_TYPE_ETHERNET_CSMACD) return hardware ? AdapterKind::Ethernet : AdapterKind::Virtual;
    // Transition tunnels never get here (IsListedInterface), so a tunnel is a VPN or similar.
    if (ifType == IF_TYPE_PROP_VIRTUAL || ifType == IF_TYPE_PPP || ifType == IF_TYPE_TUNNEL)
        return AdapterKind::Virtual;
    return AdapterKind::Other;
}

bool IsRecognizableAdapter(ULONG ifType, std::optional<bool> hardwareInterface, bool up, bool hadTraffic) {
    const bool wifiDirect = ifType == IF_TYPE_IEEE80211 && hardwareInterface.has_value() && !*hardwareInterface;
    if (wifiDirect) return up;
    return up || hadTraffic;
}

std::optional<std::vector<AdapterInfo>> TryEnumerateAdapters() {
    std::vector<BYTE> buf;
    const ULONG rc = CallWithGrowingBuffer(buf, 16 * 1024, 5, [](BYTE* data, ULONG* size) -> ULONG {
        const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
        return GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data), size);
    });
    std::vector<AdapterInfo> out;
    if (rc == ERROR_NO_DATA) return out;  // no adapters at all
    if (rc != NO_ERROR) return std::nullopt;

    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (!a->AdapterName) continue;
        if (!IsListedInterface(a->IfType, a->TunnelType)) continue;

        std::optional<bool> hardware;
        bool hadTraffic = true;  // unknown: keep it listed
        MIB_IF_ROW2 row{};
        row.InterfaceLuid = a->Luid;
        if (GetIfEntry2(&row) == NO_ERROR) {
            hardware = row.InterfaceAndOperStatusFlags.HardwareInterface != 0;
            hadTraffic = row.InOctets != 0 || row.OutOctets != 0;
        }

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
        info.recognizable = IsRecognizableAdapter(a->IfType, hardware, info.up, hadTraffic);
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<AdapterInfo> EnumerateAdapters() { return TryEnumerateAdapters().value_or(std::vector<AdapterInfo>{}); }

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
