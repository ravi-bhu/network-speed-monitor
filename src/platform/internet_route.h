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

// The coalescing rule behind NetChangeWatcher (exposed for tests). Notify() posts `msg` to the armed window
// only when no earlier post is still pending; a failed post leaves nothing pending. Before Arm() and after
// Disarm() it does nothing, so nothing is posted to a null window (which would reach the calling thread).
// Notify() may run on any thread; Arm() and Disarm() must not run concurrently with it.
class ChangeCoalescer {
public:
    using PostFn = BOOL(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
    explicit ChangeCoalescer(PostFn post = &PostMessageW) : post_(post) {}

    void Arm(HWND notify, UINT msg);  // also clears the pending flag
    void Disarm();
    void Notify();
    void Acknowledge() { pending_ = false; }
    bool IsPending() const { return pending_; }

private:
    PostFn post_;
    HWND hwnd_ = nullptr;
    UINT msg_ = 0;
    std::atomic<bool> pending_{false};
};

// Watches route and IP-interface changes. Callbacks run on threadpool threads and post `msg`
// to `notify` only when no earlier notification is still pending (coalescing).
// The app MUST call Acknowledge() when it handles `msg`, otherwise no further message is posted.
// Start, Stop and the destructor are not thread-safe against each other: call them from one thread.
class NetChangeWatcher {
public:
    NetChangeWatcher() = default;
    NetChangeWatcher(const NetChangeWatcher&) = delete;
    NetChangeWatcher& operator=(const NetChangeWatcher&) = delete;
    ~NetChangeWatcher();

    bool Start(HWND notify, UINT msg);
    void Stop();  // safe to call repeatedly
    bool IsPending() const { return coalescer_.IsPending(); }
    void Acknowledge() { coalescer_.Acknowledge(); }  // clears the pending flag; call from the message handler

private:
    static void CALLBACK OnRouteChange(PVOID ctx, PMIB_IPFORWARD_ROW2, MIB_NOTIFICATION_TYPE);
    static void CALLBACK OnInterfaceChange(PVOID ctx, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE);

    ChangeCoalescer coalescer_;
    HANDLE routeHandle_ = nullptr;
    HANDLE ifHandle_ = nullptr;
};

}  // namespace nsm
