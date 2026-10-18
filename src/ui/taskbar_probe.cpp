#include "ui/taskbar_probe.h"

#include <objbase.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <uiautomation.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdlib>

namespace nsm {
namespace {

using Microsoft::WRL::ComPtr;

bool IsHorizontal(Edge e) { return e == Edge::Bottom || e == Edge::Top; }

Rect ToRect(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }

Rect WindowRect(HWND hwnd) {
    RECT r{};
    GetWindowRect(hwnd, &r);
    return ToRect(r);
}

bool Intersects(const Rect& a, const Rect& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

Rect Union(const Rect& a, const Rect& b) {
    if (a.Empty()) return b;
    return {std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom)};
}

// One IUIAutomation per process, created on first successful use. Intentionally never released:
// releasing it from a static destructor after CoUninitialize would crash at exit.
IUIAutomation* Uia() {
    static IUIAutomation* uia = nullptr;
    if (!uia) {
        CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
    }
    return uia;
}

// Bounding rects of every Button descendant of `root` (task buttons; from the whole taskbar
// also Start, Search, ...).
std::vector<Rect> ButtonRects(HWND rootWindow) {
    std::vector<Rect> out;
    IUIAutomation* uia = Uia();
    if (!uia) return out;

    ComPtr<IUIAutomationElement> root;
    if (FAILED(uia->ElementFromHandle(rootWindow, &root)) || !root) return out;

    VARIANT button;
    VariantInit(&button);
    button.vt = VT_I4;
    button.lVal = UIA_ButtonControlTypeId;
    ComPtr<IUIAutomationCondition> isButton;
    if (FAILED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, button, &isButton))) return out;

    ComPtr<IUIAutomationCacheRequest> cache;
    if (FAILED(uia->CreateCacheRequest(&cache)) || FAILED(cache->AddProperty(UIA_BoundingRectanglePropertyId)))
        return out;

    ComPtr<IUIAutomationElementArray> found;
    if (FAILED(root->FindAllBuildCache(TreeScope_Descendants, isButton.Get(), cache.Get(), &found)) || !found)
        return out;

    int count = 0;
    found->get_Length(&count);
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> el;
        RECT r{};
        if (SUCCEEDED(found->GetElement(i, &el)) && el && SUCCEEDED(el->get_CachedBoundingRectangle(&r)))
            out.push_back(ToRect(r));
    }
    return out;
}

HWND FindChildByClass(HWND parent, const wchar_t* cls) {
    struct Search {
        const wchar_t* cls;
        HWND found;
    } s{cls, nullptr};
    EnumChildWindows(
        parent,
        [](HWND h, LPARAM lp) -> BOOL {
            auto* s = reinterpret_cast<Search*>(lp);
            wchar_t name[64];
            if (GetClassNameW(h, name, static_cast<int>(std::size(name))) && wcscmp(name, s->cls) == 0) {
                s->found = h;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&s));
    return s.found;
}

// The clock on a secondary taskbar: a ClockButton window on Win10, a UIA element on Win11.
bool FindSecondaryClock(HWND taskbar, Rect& out) {
    if (HWND clock = FindChildByClass(taskbar, L"ClockButton")) {
        out = WindowRect(clock);
        return !out.Empty();
    }
    IUIAutomation* uia = Uia();
    if (!uia) return false;
    ComPtr<IUIAutomationElement> root;
    if (FAILED(uia->ElementFromHandle(taskbar, &root)) || !root) return false;
    VARIANT cls;
    VariantInit(&cls);
    cls.vt = VT_BSTR;
    cls.bstrVal = SysAllocString(L"ClockButton");
    ComPtr<IUIAutomationCondition> cond;
    const HRESULT hr = uia->CreatePropertyCondition(UIA_ClassNamePropertyId, cls, &cond);
    VariantClear(&cls);
    if (FAILED(hr)) return false;
    ComPtr<IUIAutomationElement> clock;
    RECT r{};
    if (FAILED(root->FindFirst(TreeScope_Descendants, cond.Get(), &clock)) || !clock ||
        FAILED(clock->get_CurrentBoundingRectangle(&r)))
        return false;
    out = ToRect(r);
    return !out.Empty();
}

Edge EdgeFromAppBar(UINT edge) {
    switch (edge) {
        case ABE_TOP: return Edge::Top;
        case ABE_LEFT: return Edge::Left;
        case ABE_RIGHT: return Edge::Right;
        default: return Edge::Bottom;
    }
}

// Where the UIA button search starts. On Windows 10 the task buttons all live under the
// MSTaskListWClass window, a much smaller subtree than the whole taskbar (which also holds every
// tray icon); the buttons left of it (Start, Search, Task View) never decide the placement. On
// Windows 11 that window is absent or hidden, so search the whole taskbar.
HWND OccupiedRoot(HWND taskbar) {
    if (IsWindows11()) return taskbar;
    HWND list = FindChildByClass(taskbar, L"MSTaskListWClass");
    return list && IsWindowVisible(list) ? list : taskbar;
}

TaskbarInfo Probe(HWND hwnd, bool primary, bool measureOccupied) {
    TaskbarInfo info{};
    info.hwnd = hwnd;
    info.primary = primary;
    info.monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    TaskbarGeometry& g = info.geom;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(info.monitor, &mi);
    g.monitor = ToRect(mi.rcMonitor);
    g.taskbar = WindowRect(hwnd);

    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(info.monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
    g.dpi = dpiX;

    bool trayFound = false;
    if (primary) {
        APPBARDATA abd{sizeof(abd)};
        abd.hWnd = hwnd;
        g.edge = SHAppBarMessage(ABM_GETTASKBARPOS, &abd) ? EdgeFromAppBar(abd.uEdge)
                                                           : EdgeFromRects(g.taskbar, g.monitor);
        if (HWND tray = FindWindowExW(hwnd, nullptr, L"TrayNotifyWnd", nullptr)) {
            g.tray = WindowRect(tray);
            trayFound = !g.tray.Empty();
        }
    } else {
        g.edge = EdgeFromRects(g.taskbar, g.monitor);
        trayFound = FindSecondaryClock(hwnd, g.tray);
    }
    if (!trayFound) g.tray = FallbackTray(g.taskbar, g.edge);
    info.trayFound = trayFound;

    if (measureOccupied) g.occupied = OccupiedExtent(ButtonRects(OccupiedRoot(hwnd)), g.taskbar, g.tray, g.edge);
    return info;
}

}  // namespace

Edge EdgeFromRects(const Rect& taskbar, const Rect& monitor) {
    if (taskbar.Width() >= taskbar.Height()) {
        const int toTop = std::abs(taskbar.top - monitor.top);
        const int toBottom = std::abs(monitor.bottom - taskbar.bottom);
        return toTop < toBottom ? Edge::Top : Edge::Bottom;
    }
    const int toLeft = std::abs(taskbar.left - monitor.left);
    const int toRight = std::abs(monitor.right - taskbar.right);
    return toLeft < toRight ? Edge::Left : Edge::Right;
}

Rect FallbackTray(const Rect& taskbar, Edge edge) {
    if (IsHorizontal(edge)) return {taskbar.right, taskbar.top, taskbar.right, taskbar.bottom};
    return {taskbar.left, taskbar.bottom, taskbar.right, taskbar.bottom};
}

Rect OccupiedExtent(const std::vector<Rect>& buttons, const Rect& taskbar, const Rect& tray, Edge edge) {
    Rect occupied;
    for (const Rect& b : buttons) {
        if (b.Empty() || !Intersects(b, taskbar)) continue;
        const bool before = IsHorizontal(edge) ? b.right <= tray.left : b.bottom <= tray.top;
        if (before) occupied = Union(occupied, b);
    }
    return occupied;
}

std::vector<TaskbarInfo> ProbeTaskbars(OccupiedProbe occupied) {
    std::vector<TaskbarInfo> out;
    if (HWND primary = FindWindowW(L"Shell_TrayWnd", nullptr))
        out.push_back(Probe(primary, true, occupied == OccupiedProbe::All));
    for (HWND h = FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr); h;
         h = FindWindowExW(nullptr, h, L"Shell_SecondaryTrayWnd", nullptr)) {
        out.push_back(Probe(h, false, occupied != OccupiedProbe::None));
    }
    return out;
}

bool IsWindows11() {
    static const bool result = [] {
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        auto fn = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
        RTL_OSVERSIONINFOW v{sizeof(v)};
        return fn && fn(&v) == 0 && v.dwBuildNumber >= 22000;
    }();
    return result;
}

}  // namespace nsm
