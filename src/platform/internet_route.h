#pragma once
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <atomic>
#include <optional>

namespace nsm {

// The interface Windows would use to reach the internet (local routing-table lookup only).
std::optional<NET_LUID> FindInternetInterface();

// Watches route and IP-interface changes. Callbacks run on threadpool threads and post `msg`
// to `notify` only when no earlier notification is still pending (coalescing).
// The app MUST call Acknowledge() when it handles `msg`, otherwise no further message is posted.
class NetChangeWatcher {
public:
    NetChangeWatcher() = default;
    NetChangeWatcher(const NetChangeWatcher&) = delete;
    NetChangeWatcher& operator=(const NetChangeWatcher&) = delete;
    ~NetChangeWatcher();

    bool Start(HWND notify, UINT msg);
    void Stop();  // safe to call repeatedly
    bool IsPending() const { return pending_; }
    void Notify();  // what the OS callbacks call; public so tests can drive it
    void Acknowledge();  // clears the pending flag; call from the message handler

private:
    static void CALLBACK OnRouteChange(PVOID ctx, PMIB_IPFORWARD_ROW2, MIB_NOTIFICATION_TYPE);
    static void CALLBACK OnInterfaceChange(PVOID ctx, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE);

    HWND hwnd_ = nullptr;
    UINT msg_ = 0;
    HANDLE routeHandle_ = nullptr;
    HANDLE ifHandle_ = nullptr;
    std::atomic<bool> pending_{false};
};

}  // namespace nsm
