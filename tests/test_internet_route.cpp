#include <algorithm>
#include <cstdint>

#include "doctest.h"
#include "platform/adapters.h"
#include "platform/internet_route.h"

using namespace nsm;

namespace {
struct FakePoster {
    static inline int calls = 0;
    static inline BOOL result = TRUE;
    static inline HWND lastHwnd = nullptr;
    static inline UINT lastMsg = 0;
    static void Reset() {
        calls = 0;
        result = TRUE;
        lastHwnd = nullptr;
        lastMsg = 0;
    }
    static BOOL WINAPI Post(HWND hwnd, UINT msg, WPARAM, LPARAM) {
        ++calls;
        lastHwnd = hwnd;
        lastMsg = msg;
        return result;
    }
};

HWND FakeWindow() { return reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1234)); }
}  // namespace

TEST_CASE("ChangeCoalescer does nothing before Arm") {
    FakePoster::Reset();
    ChangeCoalescer c(&FakePoster::Post);
    c.Notify();
    CHECK(FakePoster::calls == 0);
    CHECK(!c.IsPending());  // nothing sticks, so the first change after Arm still posts
    c.Arm(FakeWindow(), WM_APP + 1);
    c.Notify();
    CHECK(FakePoster::calls == 1);
    CHECK(FakePoster::lastHwnd == FakeWindow());
    CHECK(FakePoster::lastMsg == WM_APP + 1);
}

TEST_CASE("ChangeCoalescer posts once until acknowledged") {
    FakePoster::Reset();
    ChangeCoalescer c(&FakePoster::Post);
    c.Arm(FakeWindow(), WM_APP + 1);
    c.Notify();
    c.Notify();
    c.Notify();
    CHECK(FakePoster::calls == 1);
    CHECK(c.IsPending());
    c.Acknowledge();
    CHECK(!c.IsPending());
    c.Notify();
    CHECK(FakePoster::calls == 2);
    CHECK(c.IsPending());
}

TEST_CASE("ChangeCoalescer clears pending when the post fails") {
    FakePoster::Reset();
    ChangeCoalescer c(&FakePoster::Post);
    c.Arm(FakeWindow(), WM_APP + 1);
    FakePoster::result = FALSE;  // window gone or queue full
    c.Notify();
    CHECK(FakePoster::calls == 1);
    CHECK(!c.IsPending());
    FakePoster::result = TRUE;
    c.Notify();  // a later change is not suppressed
    CHECK(FakePoster::calls == 2);
    CHECK(c.IsPending());
}

TEST_CASE("ChangeCoalescer stops posting after Disarm") {
    FakePoster::Reset();
    ChangeCoalescer c(&FakePoster::Post);
    c.Arm(FakeWindow(), WM_APP + 1);
    c.Notify();
    REQUIRE(c.IsPending());
    c.Disarm();
    c.Acknowledge();
    c.Notify();
    CHECK(FakePoster::calls == 1);
    CHECK(!c.IsPending());
    c.Arm(FakeWindow(), WM_APP + 2);
    c.Notify();
    CHECK(FakePoster::calls == 2);
    CHECK(FakePoster::lastMsg == WM_APP + 2);
}

TEST_CASE("ChangeCoalescer re-arming drops a stale pending flag") {
    FakePoster::Reset();
    ChangeCoalescer c(&FakePoster::Post);
    c.Arm(FakeWindow(), WM_APP + 1);
    c.Notify();
    REQUIRE(c.IsPending());
    c.Arm(FakeWindow(), WM_APP + 1);  // e.g. Start after Stop without an Acknowledge
    CHECK(!c.IsPending());
    c.Notify();
    CHECK(FakePoster::calls == 2);
}

TEST_CASE("FindInternetInterface agrees with EnumerateAdapters' filter when online" *
          doctest::test_suite("integration")) {
    auto luid = FindInternetInterface();
    if (!luid) {
        MESSAGE("offline; skipped");
        return;
    }
    // The best route can use an interface EnumerateAdapters drops (loopback, transition tunnels), so the
    // check is that the LUID is listed exactly when the filter keeps it, not that it is always listed.
    MIB_IF_ROW2 row{};
    row.InterfaceLuid = *luid;
    REQUIRE(GetIfEntry2(&row) == NO_ERROR);
    auto list = EnumerateAdapters();
    bool listed = std::any_of(list.begin(), list.end(), [&](auto& a) { return a.luid.Value == luid->Value; });
    CHECK(listed == IsListedInterface(row.Type, row.TunnelType));
}

TEST_CASE("NetChangeWatcher starts, stops and restarts" * doctest::test_suite("integration")) {
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    REQUIRE(hwnd != nullptr);
    {
        NetChangeWatcher w;
        CHECK(w.Start(hwnd, WM_APP + 1));
        w.Stop();
        w.Stop();  // idempotent
        CHECK(w.Start(hwnd, WM_APP + 1));
        w.Acknowledge();
        w.Stop();
    }  // destructor after Stop is safe
    {
        NetChangeWatcher w;
        CHECK(w.Start(hwnd, WM_APP + 1));
    }  // destructor stops
    DestroyWindow(hwnd);
}

TEST_CASE("NetChangeWatcher posts nothing at Start" * doctest::test_suite("integration")) {
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    REQUIRE(hwnd != nullptr);
    NetChangeWatcher w;
    REQUIRE(w.Start(hwnd, WM_APP + 1));
    // Stop waits for in-flight callbacks, so anything registration triggered has been posted by now.
    // No sleep: a real network change can only land in the short gap between Start and Stop.
    w.Stop();
    MSG m{};
    CHECK(!PeekMessageW(&m, hwnd, WM_APP + 1, WM_APP + 1, PM_REMOVE));
    CHECK(!w.IsPending());
    DestroyWindow(hwnd);
}
