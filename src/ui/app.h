#pragma once
#include "platform/adapters.h"  // first: brings in winsock2.h before windows.h

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/sampler.h"
#include "core/speed_history.h"
#include "core/time_zone.h"
#include "core/usage_accumulator.h"
#include "platform/internet_route.h"
#include "platform/network_identity.h"
#include "platform/settings.h"
#include "storage/usage_store.h"
#include "ui/overlay_manager.h"
#include "ui/report_window.h"

namespace nsm {

inline constexpr wchar_t kSingleInstanceMutex[] =
    L"Local\\NetworkSpeedMonitor.SingleInstance.7d3c6a52-0c8e-4f0f-9b1e-5a7f2c1d9e43";
inline constexpr wchar_t kAppWindowClass[] = L"NetworkSpeedMonitor.AppWindow";
inline constexpr UINT WM_APP_SHOWREPORT = WM_APP + 1;
inline constexpr UINT WM_APP_NETCHANGED = WM_APP + 2;
inline constexpr UINT WM_APP_SHOWMENU = WM_APP + 3;  // private: right-click menu, shown from the top-level loop

// The real app: one hidden top-level window that samples every adapter once a second, feeds the
// taskbar overlay, records usage to SQLite and owns the right-click menu. UI thread only.
class App {
public:
    App() = default;
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // cmdLine: the full command line (GetCommandLineW). Returns the process exit code.
    int Run(HINSTANCE inst, const std::wstring& cmdLine);
    void ShowReport(int tab = 0);  // open (or bring forward) the report window on a tab
    void FlushNow();               // write the accumulator to the store

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    bool Start(HINSTANCE inst, const std::wstring& cmdLine);
    void OpenStore();        // sets recording_; on recovery, the report's corruption notice
    void RetryOpenStore();   // a later OpenStore after a failed one (e.g. the disk was busy at login)
    void RefreshNetwork();
    void RefreshNetworkOnce();
    void SaveNetworkMeta();  // upserts adapter and network names the store does not hold yet
    void Tick();
    void RollUpNow();
    void ShowMenu(POINT pt);
    void ApplyOptions();  // overlay options from settings_, then save
    void ShowSettings();  // the Settings dialog; OK applies through ApplyOptions
    void SaveSettingsFile();
    void UpdateTooltip();
    void ReloadAdapterNames();  // after a rename or hide in the report
    void Exit();

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    std::filesystem::path dataDir_;
    Settings settings_;

    UsageStore store_;
    bool recording_ = false;              // false when the database could not be opened at all
    std::filesystem::path recoveredFile_;  // set when a corrupt database was moved aside (report banner)
    TimeZone tz_ = TimeZone::Local();      // a snapshot: refreshed on WM_TIMECHANGE/WM_SETTINGCHANGE and per RollUp

    OverlayManager overlay_;
    std::unique_ptr<ReportWindow> report_;  // the object stays; its window exists only while open
    std::optional<ReportTheme> reportTheme_;  // --theme light|dark|contrast (Debug builds)
    NetChangeWatcher watcher_;
    Sampler sampler_;
    SpeedHistory history_;
    UsageAccumulator accumulator_;

    std::vector<AdapterInfo> adapters_;  // as of the last network refresh
    std::vector<AdapterMeta> metas_;     // stored names, friendly names and hidden flags
    std::vector<NetworkMeta> storedNetworks_;  // as read back at the last SaveNetworkMeta, to skip no-op writes
    std::map<std::wstring, NetworkIdentity> networks_;
    std::wstring internetId_;
    std::wstring graphAdapterId_;         // the adapter history_ holds samples of
    std::vector<AdapterSample> samples_;  // Tick's buffer, reused so its strings keep their capacity

    int64_t lastFlushUtc_ = 0;
    uint64_t lastRollUpMs_ = 0;  // GetTickCount64 at the last rollup
    bool ticking_ = false;
    bool refreshing_ = false;
    bool refreshAgain_ = false;
    bool exiting_ = false;
    bool menuPending_ = false;
    bool settingsOpen_ = false;
    POINT menuPt_{};
    std::FILE* trace_ = nullptr;  // --trace <file> (Debug builds): one CSV line per tick
    std::filesystem::path dataDirOverride_;  // --data-dir <path> (Debug builds): use a test data folder
};

// Pure rules behind App (exposed for tests).

// The adapter whose speed the overlay shows: the pinned one, or the internet adapter in Automatic
// mode (empty pinnedId). A pinned adapter that is gone is still returned, so the overlay shows a dash.
// Returns a reference to one of the arguments.
const std::wstring& DisplayAdapterId(const std::wstring& pinnedId, const std::wstring& internetId);
std::wstring AdapterIdForLuid(const std::vector<AdapterInfo>& adapters, std::optional<NET_LUID> luid);
// The adapter's network key, or a reference to an empty string. Valid while `networks` is unchanged.
const std::wstring& NetworkKeyOf(const std::map<std::wstring, NetworkIdentity>& networks,
                                 const std::wstring& adapterId);

// Adds one tick's deltas to the accumulator in the hour bucket: an Adapter row per adapter under that
// adapter's network, and the Internet row under the internet adapter's network.
void RecordTick(UsageAccumulator& acc, int64_t hourStartUtc, const TickResult& t,
                const std::map<std::wstring, NetworkIdentity>& networks);

// The names to upsert: what differs from the store's rows as last read back. A write that failed is
// not in the store, so it is offered again at the next refresh.
std::vector<AdapterInfo> AdaptersToSave(const std::vector<AdapterInfo>& present,
                                        const std::vector<AdapterMeta>& stored);  // name or kind differs
std::vector<NetworkMeta> NetworksToSave(const std::map<std::wstring, NetworkIdentity>& networks,
                                        const std::vector<NetworkMeta>& stored);  // keyed only, each key once

// The mini graph shows one adapter: when the displayed adapter changes (pinned to another, or Automatic
// following a new internet adapter), clears `history` and records the new id. True if it changed.
bool FollowDisplayAdapter(std::wstring& shownId, const std::wstring& displayId, SpeedHistory& history);
// The user's friendly name if set, else the Windows name (live, then stored), else the id.
std::wstring DisplayNameOf(const std::wstring& id, const std::vector<AdapterInfo>& present,
                           const std::vector<AdapterMeta>& stored);

struct AdapterMenuItem {
    std::wstring id;  // empty = Automatic
    std::wstring label;
    bool checked = false;
};
// Automatic first, then the present adapters that are not hidden, by display name. The pinned
// adapter is listed even when it is absent or hidden, so the check mark always has a place.
std::vector<AdapterMenuItem> BuildAdapterMenu(const std::vector<AdapterInfo>& present,
                                              const std::vector<AdapterMeta>& stored, const std::wstring& pinnedId,
                                              const std::wstring& automaticLabel);
std::wstring EscapeMenuText(const std::wstring& text);  // '&' -> "&&"

// Automatic: autoFormat with %s replaced by the internet adapter's name (autoNone without one).
// Pinned: the adapter's display name.
std::wstring TooltipText(const std::wstring& pinnedId, const std::wstring& internetId,
                         const std::vector<AdapterInfo>& present, const std::vector<AdapterMeta>& stored,
                         const std::wstring& autoFormat, const std::wstring& autoNone);

// The single-instance check, from CreateMutexW's result and GetLastError() straight after it.
bool OtherInstanceRunning(bool handleValid, DWORD lastError);

bool RollUpDue(uint64_t lastMs, uint64_t nowMs);  // once every 24 h of uptime

// At a flush: try opening the database again when the last Open failed (nothing is being recorded),
// so a transient failure at login does not lose the whole session. Never without a data folder.
bool ShouldRetryOpen(bool recording, bool haveDataDir);

// Hands every accumulated row to `write`. If it fails, the rows go back into the accumulator, so
// the next flush retries them with no loss and no double count. True if nothing is left unwritten.
bool FlushAccumulator(UsageAccumulator& acc, const std::function<bool(std::span<const UsageRow>)>& write);

// The value after `flag` on a full command line (argv[0] included), or empty.
std::wstring ArgValue(const std::wstring& cmdLine, const std::wstring& flag);

}  // namespace nsm
