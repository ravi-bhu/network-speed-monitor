#include "platform/internet_route.h"

namespace nsm {

std::optional<NET_LUID> FindInternetInterface() {
    // GetBestInterfaceEx is a local routing-table lookup: it sends no packets. Because it asks
    // the stack which route wins for a public address, it also handles split-default VPN routes
    // (0.0.0.0/1 + 128.0.0.0/1) that a search for a 0.0.0.0/0 route would miss.
    // Addresses are filled in by hand so the exe never needs ws2_32 (inet_pton/htonl).
    SOCKADDR_IN v4 = {};
    v4.sin_family = AF_INET;
    v4.sin_addr.S_un.S_un_b = {8, 8, 8, 8};

    SOCKADDR_IN6 v6 = {};
    v6.sin6_family = AF_INET6;
    const unsigned char a6[16] = {0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0x88, 0x88};
    for (int i = 0; i < 16; ++i) v6.sin6_addr.u.Byte[i] = a6[i];

    DWORD index = 0;
    if (GetBestInterfaceEx(reinterpret_cast<SOCKADDR*>(&v4), &index) != NO_ERROR &&
        GetBestInterfaceEx(reinterpret_cast<SOCKADDR*>(&v6), &index) != NO_ERROR) {
        return std::nullopt;
    }
    NET_LUID luid = {};
    if (ConvertInterfaceIndexToLuid(index, &luid) != NO_ERROR) return std::nullopt;
    return luid;
}

NetChangeWatcher::~NetChangeWatcher() { Stop(); }

void NetChangeWatcher::Notify() {
    bool expected = false;
    // If the post fails (window gone, queue full) clear the flag so later changes aren't suppressed.
    if (pending_.compare_exchange_strong(expected, true) && !PostMessageW(hwnd_, msg_, 0, 0)) pending_ = false;
}

void CALLBACK NetChangeWatcher::OnRouteChange(PVOID ctx, PMIB_IPFORWARD_ROW2, MIB_NOTIFICATION_TYPE) {
    static_cast<NetChangeWatcher*>(ctx)->Notify();
}

void CALLBACK NetChangeWatcher::OnInterfaceChange(PVOID ctx, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) {
    static_cast<NetChangeWatcher*>(ctx)->Notify();
}

bool NetChangeWatcher::Start(HWND notify, UINT msg) {
    Stop();
    hwnd_ = notify;
    msg_ = msg;
    pending_ = false;
    // initialNotification=FALSE: no spurious callback at registration.
    if (NotifyRouteChange2(AF_UNSPEC, &OnRouteChange, this, FALSE, &routeHandle_) != NO_ERROR) {
        routeHandle_ = nullptr;
        return false;
    }
    if (NotifyIpInterfaceChange(AF_UNSPEC, &OnInterfaceChange, this, FALSE, &ifHandle_) != NO_ERROR) {
        ifHandle_ = nullptr;
        Stop();
        return false;
    }
    return true;
}

void NetChangeWatcher::Stop() {
    // CancelMibChangeNotify2 waits for in-flight callbacks, so none run after it returns.
    if (routeHandle_) {
        CancelMibChangeNotify2(routeHandle_);
        routeHandle_ = nullptr;
    }
    if (ifHandle_) {
        CancelMibChangeNotify2(ifHandle_);
        ifHandle_ = nullptr;
    }
}

void NetChangeWatcher::Acknowledge() { pending_ = false; }

}  // namespace nsm
