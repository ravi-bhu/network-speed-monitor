#include "ui/app.h"

#include <objbase.h>
#include <shellapi.h>
#include <windowsx.h>

#include <share.h>

#include <algorithm>
#include <ctime>

#include "core/periods.h"
#include "platform/autostart.h"
#include "platform/paths.h"
#include "platform/strings.h"
#include "resource.h"

namespace nsm {

namespace {

constexpr UINT_PTR kTickTimer = 1;
constexpr UINT_PTR kRefreshTimer = 2;  // one-shot re-query after a change: Windows names a new network a few seconds late
constexpr UINT kTickMs = 1000;
constexpr UINT kRefreshDelayMs = 10'000;
constexpr uint64_t kRollUpIntervalMs = 24ull * 3600 * 1000;

enum Command : UINT {
    kCmdOpenReport = 1,
    kCmdSettings,
    kCmdExit,
    kCmdBits,
    kCmdBytes,
    kCmdShowGraph,
    kCmdLetters,
    kCmdAdapterFirst = 1000,  // + index into the adapter menu model
};

int64_t NowUtc() { return static_cast<int64_t>(_time64(nullptr)); }

const AdapterInfo* FindPresent(const std::vector<AdapterInfo>& present, const std::wstring& id) {
    for (const auto& a : present)
        if (a.id == id) return &a;
    return nullptr;
}

const AdapterMeta* FindStored(const std::vector<AdapterMeta>& stored, const std::wstring& id) {
    for (const auto& m : stored)
        if (m.id == id) return &m;
    return nullptr;
}

bool LessByName(const AdapterMenuItem& a, const AdapterMenuItem& b) {
    const int c = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a.label.c_str(),
                                  static_cast<int>(a.label.size()), b.label.c_str(), static_cast<int>(b.label.size()),
                                  nullptr, nullptr, 0);
    if (c != 0 && c != CSTR_EQUAL) return c == CSTR_LESS_THAN;
    return a.id < b.id;
}

}  // namespace

// ---- Pure rules ----

std::wstring DisplayAdapterId(const std::wstring& pinnedId, const std::wstring& internetId) {
    return pinnedId.empty() ? internetId : pinnedId;
}

std::wstring AdapterIdForLuid(const std::vector<AdapterInfo>& adapters, std::optional<NET_LUID> luid) {
    if (!luid) return {};
    for (const auto& a : adapters)
        if (a.luid.Value == luid->Value) return a.id;
    return {};
}

std::wstring NetworkKeyOf(const std::map<std::wstring, NetworkIdentity>& networks, const std::wstring& adapterId) {
    const auto it = networks.find(adapterId);
    return it != networks.end() ? it->second.key : std::wstring{};
}

std::wstring DisplayNameOf(const std::wstring& id, const std::vector<AdapterInfo>& present,
                           const std::vector<AdapterMeta>& stored) {
    const AdapterMeta* meta = FindStored(stored, id);
    if (meta && !meta->friendlyName.empty()) return meta->friendlyName;
    if (const AdapterInfo* info = FindPresent(present, id); info && !info->name.empty()) return info->name;
    if (meta && !meta->name.empty()) return meta->name;
    return id;
}

std::vector<AdapterMenuItem> BuildAdapterMenu(const std::vector<AdapterInfo>& present,
                                              const std::vector<AdapterMeta>& stored, const std::wstring& pinnedId,
                                              const std::wstring& automaticLabel) {
    std::vector<AdapterMenuItem> adapters;
    bool pinnedListed = false;
    for (const auto& a : present) {
        const AdapterMeta* meta = FindStored(stored, a.id);
        const bool pinned = !pinnedId.empty() && a.id == pinnedId;
        if (meta && meta->hidden && !pinned) continue;
        adapters.push_back(AdapterMenuItem{a.id, DisplayNameOf(a.id, present, stored), pinned});
        pinnedListed = pinnedListed || pinned;
    }
    if (!pinnedId.empty() && !pinnedListed) {
        adapters.push_back(AdapterMenuItem{pinnedId, DisplayNameOf(pinnedId, present, stored), true});
    }
    std::stable_sort(adapters.begin(), adapters.end(), LessByName);

    std::vector<AdapterMenuItem> out;
    out.reserve(adapters.size() + 1);
    out.push_back(AdapterMenuItem{L"", automaticLabel, pinnedId.empty()});
    out.insert(out.end(), adapters.begin(), adapters.end());
    return out;
}

std::wstring EscapeMenuText(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t c : text) {
        out += c;
        if (c == L'&') out += L'&';
    }
    return out;
}

std::wstring TooltipText(const std::wstring& pinnedId, const std::wstring& internetId,
                         const std::vector<AdapterInfo>& present, const std::vector<AdapterMeta>& stored,
                         const std::wstring& autoFormat, const std::wstring& autoNone) {
    if (!pinnedId.empty()) return DisplayNameOf(pinnedId, present, stored);
    if (internetId.empty()) return autoNone;
    std::wstring text = autoFormat;
    if (const size_t at = text.find(L"%s"); at != std::wstring::npos) {
        text.replace(at, 2, DisplayNameOf(internetId, present, stored));
    }
    return text;
}

bool RollUpDue(uint64_t lastMs, uint64_t nowMs) { return nowMs - lastMs >= kRollUpIntervalMs; }

bool FlushAccumulator(UsageAccumulator& acc, const std::function<bool(std::span<const UsageRow>)>& write) {
    if (acc.Empty()) return true;
    const std::vector<UsageRow> rows = acc.Take();
    if (write(rows)) return true;
    for (const auto& r : rows) acc.Add(r.hourUtc, r.series, r.adapterId, r.networkKey, r.rx, r.tx);
    return false;
}

std::wstring ArgValue(const std::wstring& cmdLine, const std::wstring& flag) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine.c_str(), &argc);
    if (!argv) return {};
    std::wstring value;
    for (int i = 1; i + 1 < argc; ++i) {
        if (flag == argv[i]) {
            value = argv[i + 1];
            break;
        }
    }
    LocalFree(argv);
    return value;
}

// ---- App ----

App::~App() {
    if (trace_) std::fclose(trace_);
}

int App::Run(HINSTANCE inst, const std::wstring& cmdLine) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        // Already running: ask that instance to show its report, and leave.
        if (HWND other = FindWindowW(kAppWindowClass, nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(other, WM_APP_SHOWREPORT, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }

    int code = 1;
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {  // UIA and the Network List Manager need an STA
        if (Start(inst, cmdLine)) {
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (msg.hwnd == hwnd_ && msg.message == WM_APP_SHOWMENU) {
                    menuPending_ = true;
                    menuPt_ = POINT{GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
                } else {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                // The menu runs from here, the top-level loop, never from inside a nested (COM/UIA)
                // pump that happened to dispatch the request.
                while (menuPending_ && !exiting_) {
                    menuPending_ = false;
                    ShowMenu(menuPt_);
                }
            }
            code = static_cast<int>(msg.wParam);
        } else if (hwnd_) {
            Exit();
        }
        watcher_.Stop();
        CoUninitialize();
    }
    if (mutex) CloseHandle(mutex);
    return code;
}

bool App::Start(HINSTANCE inst, const std::wstring& cmdLine) {
    inst_ = inst;
#ifdef _DEBUG
    if (const std::wstring trace = ArgValue(cmdLine, L"--trace"); !trace.empty()) {
        trace_ = _wfsopen(trace.c_str(), L"w", _SH_DENYWR);  // readable while the app runs
        if (trace_) {
            std::fprintf(trace_, "utc,mono,display_id,internet_id,valid,rx_Bps,tx_Bps\n");
            std::fflush(trace_);
        }
    }
#else
    (void)cmdLine;
#endif

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &App::WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kAppWindowClass;
    if (!RegisterClassExW(&wc)) return false;
    // Hidden top-level window, never shown. Not HWND_MESSAGE: it must get the TaskbarCreated and
    // WM_SETTINGCHANGE broadcasts. It also owns the overlays.
    if (!CreateWindowExW(WS_EX_TOOLWINDOW, kAppWindowClass, LoadStr(IDS_APP_NAME).c_str(), WS_POPUP, 0, 0, 0, 0,
                         nullptr, nullptr, inst, this)) {
        return false;
    }

    dataDir_ = DataDir();
    if (!dataDir_.empty()) settings_ = LoadSettings(dataDir_ / L"settings.ini");
    SetAutostart(settings_.startWithWindows);

    if (!dataDir_.empty()) {
        const UsageStore::OpenResult opened = store_.Open(dataDir_ / L"usage.db");
        recording_ = opened != UsageStore::OpenResult::Failed;  // Failed: the overlay still works, nothing is recorded
        if (opened == UsageStore::OpenResult::RecoveredFromCorruption) recoveredFile_ = store_.RecoveredFile();
    }
    RollUpNow();

    overlay_.onLeftClick = [this] { ShowReport(0); };
    // Clicks can arrive inside a COM/UIA modal pump, so the menu is only requested here.
    overlay_.onRightClick = [this](POINT pt) { PostMessageW(hwnd_, WM_APP_SHOWMENU, 0, MAKELPARAM(pt.x, pt.y)); };
    overlay_.SetOptions(OverlayOptions{settings_.allTaskbars, settings_.showGraph, settings_.letters, settings_.units});
    if (!overlay_.Init(inst, hwnd_)) return false;
    watcher_.Start(hwnd_, WM_APP_NETCHANGED);  // without it the adapter list is only read at startup
    RefreshNetwork();
    lastFlushUtc_ = NowUtc();
    return SetTimer(hwnd_, kTickTimer, kTickMs, nullptr) != 0;
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->HandleMessage(msg, wp, lp);
}

LRESULT App::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == kTickTimer) {
                Tick();
            } else if (wp == kRefreshTimer) {
                KillTimer(hwnd_, kRefreshTimer);
                RefreshNetwork();
            }
            return 0;
        case WM_APP_NETCHANGED:
            watcher_.Acknowledge();  // first, so a change during the refresh posts again
            if (exiting_) return 0;
            RefreshNetwork();
            SetTimer(hwnd_, kRefreshTimer, kRefreshDelayMs, nullptr);
            return 0;
        case WM_APP_SHOWREPORT:
            ShowReport(0);
            return 0;
        case WM_APP_SHOWMENU:
            // Dispatched by a nested pump rather than the top-level loop: defer to that loop.
            menuPending_ = true;
            menuPt_ = POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMSUSPEND) FlushNow();
            return TRUE;
        case WM_QUERYENDSESSION:
            FlushNow();
            return TRUE;
        case WM_ENDSESSION:
            overlay_.HandleMessage(msg, wp, lp);  // restores the Win10 task list
            if (wp) {
                FlushNow();
                overlay_.Shutdown();
                Exit();
            }
            return 0;
        case WM_CLOSE:  // menu Exit, taskkill without /f, or an overlay forwarding its WM_CLOSE
            Exit();
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kTickTimer);
            KillTimer(hwnd_, kRefreshTimer);
            PostQuitMessage(0);
            return 0;
        default:
            if (msg == WM_TIMECHANGE || msg == WM_SETTINGCHANGE) tz_ = TimeZone::Local();
            if (overlay_.HandleMessage(msg, wp, lp)) return 0;
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

void App::RefreshNetwork() {
    // The Network List Manager query is a COM call that can pump messages, so another change
    // notification may arrive in the middle; run again afterwards instead of nesting.
    if (refreshing_) {
        refreshAgain_ = true;
        return;
    }
    refreshing_ = true;
    do {
        refreshAgain_ = false;
        RefreshNetworkOnce();
    } while (refreshAgain_ && !exiting_);
    refreshing_ = false;
}

void App::RefreshNetworkOnce() {
    adapters_ = EnumerateAdapters();
    internetId_ = AdapterIdForLuid(adapters_, FindInternetInterface());
    networks_ = QueryAdapterNetworks();

    if (recording_) {
        // Upsert only what changed: network notifications can be frequent, and the disk is touched sparingly.
        for (const auto& a : adapters_) {
            const auto it = savedAdapters_.find(a.id);
            if (it != savedAdapters_.end() && it->second == std::make_pair(a.name, a.kind)) continue;
            store_.UpsertAdapter(a.id, a.name, a.kind);
            savedAdapters_[a.id] = {a.name, a.kind};
        }
        for (const auto& [adapterId, net] : networks_) {
            if (net.key.empty()) continue;
            const auto it = savedNetworks_.find(net.key);
            if (it != savedNetworks_.end() && it->second == net.name) continue;
            store_.UpsertNetwork(NetworkMeta{net.key, net.name});
            savedNetworks_[net.key] = net.name;
        }
        metas_ = store_.Adapters();
    }
    UpdateTooltip();
}

void App::Tick() {
    if (ticking_ || exiting_) return;  // a nested pump inside the overlay update can deliver another WM_TIMER
    ticking_ = true;

    std::vector<AdapterSample> samples;
    samples.reserve(adapters_.size());
    for (const auto& a : adapters_) {
        if (auto r = ReadCounters(a.luid, MonotonicSeconds())) samples.push_back(AdapterSample{a.id, *r});
    }
    const std::wstring displayId = DisplayAdapterId(settings_.adapterId, internetId_);
    const TickResult t = sampler_.Tick(samples, internetId_, displayId);
    const int64_t now = NowUtc();

    if (recording_) {
        for (const auto& d : t.perAdapter) {
            accumulator_.Add(now, Series::Adapter, d.adapterId, NetworkKeyOf(networks_, d.adapterId), d.rx, d.tx);
        }
        if (t.internet) {
            accumulator_.Add(now, Series::Internet, t.internet->adapterId, NetworkKeyOf(networks_, t.internet->adapterId),
                             t.internet->rx, t.internet->tx);
        }
    }

    history_.Push(t.display.value_or(Speed{}));
    if (trace_) {
        const Speed s = t.display.value_or(Speed{});
        std::fprintf(trace_, "%lld,%.3f,%ls,%ls,%d,%.1f,%.1f\n", static_cast<long long>(now), MonotonicSeconds(),
                     displayId.c_str(), internetId_.c_str(), t.display ? 1 : 0, s.rx, s.tx);
        std::fflush(trace_);
    }
    overlay_.Update(t.display, history_);

    if (ShouldFlush(lastFlushUtc_, now)) FlushNow();
    if (RollUpDue(lastRollUpMs_, GetTickCount64())) RollUpNow();
    ticking_ = false;
}

void App::FlushNow() {
    lastFlushUtc_ = NowUtc();  // also after a failure: retry at the next flush, not every second
    if (!recording_) return;
    FlushAccumulator(accumulator_, [this](std::span<const UsageRow> rows) { return store_.AddUsage(rows); });
}

void App::RollUpNow() {
    lastRollUpMs_ = GetTickCount64();
    tz_ = TimeZone::Local();
    if (recording_) store_.RollUp(NowUtc(), tz_);
}

void App::ShowReport(int tab) {
    (void)tab;  // Task 15: the report window
    OutputDebugStringW(L"NetworkSpeedMonitor: ShowReport\n");
}

void App::UpdateTooltip() {
    overlay_.SetTooltip(TooltipText(settings_.adapterId, internetId_, adapters_, metas_, LoadStr(IDS_TIP_AUTO),
                                    LoadStr(IDS_TIP_AUTO_NONE)));
}

void App::SaveSettingsFile() {
    if (!dataDir_.empty()) SaveSettings(dataDir_ / L"settings.ini", settings_);
}

void App::ApplyOptions() {
    overlay_.SetOptions(OverlayOptions{settings_.allTaskbars, settings_.showGraph, settings_.letters, settings_.units});
    SaveSettingsFile();
}

void App::ShowMenu(POINT pt) {
    if (recording_) metas_ = store_.Adapters();  // pick up renames and hides made in the report
    const std::vector<AdapterMenuItem> adapters =
        BuildAdapterMenu(adapters_, metas_, settings_.adapterId, LoadStr(IDS_MENU_AUTOMATIC));

    HMENU adapterMenu = CreatePopupMenu();
    for (size_t i = 0; i < adapters.size(); ++i) {
        AppendMenuW(adapterMenu, MF_STRING | (adapters[i].checked ? MF_CHECKED : 0u),
                    kCmdAdapterFirst + static_cast<UINT>(i), EscapeMenuText(adapters[i].label).c_str());
    }
    HMENU unitsMenu = CreatePopupMenu();
    AppendMenuW(unitsMenu, MF_STRING, kCmdBits, LoadStr(IDS_MENU_BITS).c_str());
    AppendMenuW(unitsMenu, MF_STRING, kCmdBytes, LoadStr(IDS_MENU_BYTES).c_str());
    CheckMenuRadioItem(unitsMenu, kCmdBits, kCmdBytes, settings_.units == UnitMode::Bits ? kCmdBits : kCmdBytes,
                       MF_BYCOMMAND);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(adapterMenu), LoadStr(IDS_MENU_ADAPTER).c_str());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(unitsMenu), LoadStr(IDS_MENU_UNITS).c_str());
    AppendMenuW(menu, MF_STRING | (settings_.showGraph ? MF_CHECKED : 0u), kCmdShowGraph,
                LoadStr(IDS_MENU_SHOW_GRAPH).c_str());
    AppendMenuW(menu, MF_STRING | (settings_.letters ? MF_CHECKED : 0u), kCmdLetters, LoadStr(IDS_MENU_LETTERS).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdOpenReport, LoadStr(IDS_MENU_OPEN_REPORT).c_str());
    AppendMenuW(menu, MF_STRING, kCmdSettings, LoadStr(IDS_MENU_SETTINGS).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, LoadStr(IDS_MENU_EXIT).c_str());
    SetMenuDefaultItem(menu, kCmdOpenReport, FALSE);  // what a left-click does

    // TrackPopupMenu rules: the owner must be foreground, or the menu won't close when the user
    // clicks elsewhere; WM_NULL afterwards lets a second click reopen it.
    SetForegroundWindow(hwnd_);
    const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    const UINT cmd = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align, pt.x, pt.y, hwnd_, nullptr));
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);  // and its submenus
    if (exiting_) return;

    switch (cmd) {
        case 0:
            return;
        case kCmdOpenReport:
            ShowReport(0);
            return;
        case kCmdSettings:
            return;  // Task 16: the settings dialog
        case kCmdExit:
            Exit();
            return;
        case kCmdBits:
        case kCmdBytes:
            settings_.units = cmd == kCmdBits ? UnitMode::Bits : UnitMode::Bytes;
            ApplyOptions();
            return;
        case kCmdShowGraph:
            settings_.showGraph = !settings_.showGraph;
            ApplyOptions();
            return;
        case kCmdLetters:
            settings_.letters = !settings_.letters;
            ApplyOptions();
            return;
        default:
            if (cmd >= kCmdAdapterFirst && cmd - kCmdAdapterFirst < adapters.size()) {
                settings_.adapterId = adapters[cmd - kCmdAdapterFirst].id;
                UpdateTooltip();
                SaveSettingsFile();
            }
            return;
    }
}

void App::Exit() {
    if (exiting_) return;
    exiting_ = true;
    KillTimer(hwnd_, kTickTimer);
    KillTimer(hwnd_, kRefreshTimer);
    FlushNow();
    overlay_.Shutdown();  // before any window goes: restores the Win10 task list
    watcher_.Stop();
    SaveSettingsFile();
    DestroyWindow(hwnd_);
}

}  // namespace nsm
