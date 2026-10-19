#include "doctest.h"

#include <windows.h>
#include <commctrl.h>

#include <ctime>
#include <filesystem>
#include <random>
#include <span>
#include <vector>

#include "core/periods.h"
#include "platform/strings.h"
#include "resource.h"
#include "ui/dialogs.h"
#include "ui/report_window.h"

using namespace nsm;

namespace {

bool Same(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

TableRow Row(const wchar_t* id, const wchar_t* name, const wchar_t* type, uint64_t rx, uint64_t tx) {
    TableRow r;
    r.data.id = id;
    r.data.name = name;
    r.data.totals = Totals{rx, tx};
    r.name = name;
    r.type = type;
    return r;
}

std::vector<std::wstring> Ids(const std::vector<TableRow>& rows) {
    std::vector<std::wstring> out;
    for (const auto& r : rows) out.push_back(r.data.id);
    return out;
}

constexpr RECT kWork{0, 0, 1920, 1040};
constexpr SIZE kDefault{950, 700};
constexpr SIZE kMin{750, 600};

}  // namespace

TEST_CASE("FitReportRect keeps a saved rect that is fully visible") {
    const RECT saved{100, 100, 1100, 900};
    CHECK(Same(FitReportRect(saved, kWork, kDefault, kMin), saved));
}

TEST_CASE("FitReportRect moves a rect that hangs off the work area back inside") {
    CHECK(Same(FitReportRect(RECT{1500, 700, 2500, 1500}, kWork, kDefault, kMin), RECT{920, 240, 1920, 1040}));
    CHECK(Same(FitReportRect(RECT{-300, -50, 700, 750}, kWork, kDefault, kMin), RECT{0, 0, 1000, 800}));
    // A work area that does not start at 0 (taskbar on the left, or a second monitor).
    const RECT work{-1280, 60, 0, 1024};
    CHECK(Same(FitReportRect(RECT{100, 100, 900, 700}, work, kDefault, kMin), RECT{-800, 100, 0, 700}));
}

TEST_CASE("FitReportRect shrinks a rect larger than the work area and grows one below the minimum") {
    CHECK(Same(FitReportRect(RECT{-10, -10, 3000, 2000}, kWork, kDefault, kMin), kWork));
    CHECK(Same(FitReportRect(RECT{100, 100, 200, 150}, kWork, kDefault, kMin), RECT{100, 100, 850, 700}));
    // Minimum bigger than a tiny work area: the work area wins.
    const RECT tiny{0, 0, 640, 480};
    CHECK(Same(FitReportRect(RECT{0, 0, 300, 300}, tiny, kDefault, kMin), tiny));
}

TEST_CASE("FitReportRect centres the default size when nothing was saved") {
    CHECK(Same(FitReportRect(std::nullopt, kWork, kDefault, kMin), RECT{485, 170, 1435, 870}));
}

TEST_CASE("NextSort toggles the clicked column and starts new columns in their natural direction") {
    TableSort s;  // default: Total, largest first
    CHECK(s.column == TableColumn::Total);
    CHECK_FALSE(s.ascending);
    s = NextSort(s, TableColumn::Total);
    CHECK(s.column == TableColumn::Total);
    CHECK(s.ascending);
    s = NextSort(s, TableColumn::Name);
    CHECK(s.column == TableColumn::Name);
    CHECK(s.ascending);  // text: A to Z first
    s = NextSort(s, TableColumn::Name);
    CHECK_FALSE(s.ascending);
    s = NextSort(s, TableColumn::Received);
    CHECK(s.column == TableColumn::Received);
    CHECK_FALSE(s.ascending);  // numbers: largest first
    s = NextSort(s, TableColumn::Type);
    CHECK(s.ascending);
}

TEST_CASE("SortRows orders by the chosen column in either direction") {
    std::vector<TableRow> rows{Row(L"a", L"wi-Fi", L"Wi-Fi", 500, 10), Row(L"b", L"Ethernet 2", L"Ethernet", 100, 900),
                               Row(L"c", L"Ethernet 10", L"Ethernet", 300, 0), Row(L"d", L"VPN", L"Virtual", 0, 0)};
    SortRows(rows, TableSort{TableColumn::Name, true});  // case-insensitive, digits as numbers
    CHECK(Ids(rows) == std::vector<std::wstring>{L"b", L"c", L"d", L"a"});
    SortRows(rows, TableSort{TableColumn::Name, false});
    CHECK(Ids(rows) == std::vector<std::wstring>{L"a", L"d", L"c", L"b"});
    SortRows(rows, TableSort{TableColumn::Received, false});
    CHECK(Ids(rows) == std::vector<std::wstring>{L"a", L"c", L"b", L"d"});
    SortRows(rows, TableSort{TableColumn::Sent, true});
    CHECK(Ids(rows) == std::vector<std::wstring>{L"c", L"d", L"a", L"b"});  // ties: larger total first
    SortRows(rows, TableSort{TableColumn::Total, false});
    CHECK(Ids(rows) == std::vector<std::wstring>{L"b", L"a", L"c", L"d"});
    SortRows(rows, TableSort{TableColumn::Type, true});  // Ethernet, Ethernet, Virtual, Wi-Fi; ties by total
    CHECK(Ids(rows) == std::vector<std::wstring>{L"b", L"c", L"d", L"a"});
}

TEST_CASE("DefaultCsvName uses the local date") {
    CHECK(DefaultCsvName(LocalDate{2026, 3, 7}) == L"network-usage-2026-03-07.csv");
    CHECK(DefaultCsvName(LocalDate{2026, 10, 19}) == L"network-usage-2026-10-19.csv");
}

TEST_CASE("FillPlaceholders replaces each %s in turn and never rescans inserted text") {
    CHECK(FillPlaceholders(L"↓ %s · ↑ %s", {L"1 GB", L"2 MB"}) == L"↓ 1 GB · ↑ 2 MB");
    CHECK(FillPlaceholders(L"Delete “%s”?", {L"100% %s"}) == L"Delete “100% %s”?");
    CHECK(FillPlaceholders(L"%s and %s", {L"one"}) == L"one and ");
    CHECK(FillPlaceholders(L"no placeholder", {L"x"}) == L"no placeholder");
}

TEST_CASE("RangeLabel shows one date or a from-to pair") {
    CHECK(RangeLabel(LocalDate{2026, 10, 19}, LocalDate{2026, 10, 19}, L"en-US") == L"10/19/2026");
    CHECK(RangeLabel(LocalDate{2026, 10, 1}, LocalDate{2026, 10, 19}, L"en-US") == L"10/1/2026 – 10/19/2026");
}

namespace {
std::vector<TimelineBar> Bars(std::initializer_list<LocalDateTime> starts) {
    std::vector<TimelineBar> out;
    for (const auto& s : starts) out.push_back(TimelineBar{s, {}});
    return out;
}
std::vector<TimelineBar> Days(int year, int month, int firstDay, int count) {
    std::vector<TimelineBar> out;
    for (int d = 0; d < count; ++d) {
        LocalDateTime t{year, month, firstDay + d, 0, 0, 0};
        if (t.day > 31) t = LocalDateTime{year, month + 1, t.day - 31, 0, 0, 0};
        out.push_back(TimelineBar{t, {}});
    }
    return out;
}
}  // namespace

TEST_CASE("AxisLabels: hours, keeping the minutes of half-hour time zones") {
    const auto hours =
        AxisLabels(Bars({{2026, 10, 19, 0, 0, 0}, {2026, 10, 19, 14, 0, 0}}), Granularity::Hour, L"en-US");
    CHECK(hours == std::vector<std::wstring>{L"12 AM", L"2 PM"});
    // UTC+5:30: the stored UTC hours start at half past the local hour.
    const auto india =
        AxisLabels(Bars({{2026, 10, 18, 23, 30, 0}, {2026, 10, 19, 0, 30, 0}}), Granularity::Hour, L"en-US");
    CHECK(india == std::vector<std::wstring>{L"11:30 PM", L"12:30 AM"});
}

TEST_CASE("AxisLabels: days show the weekday for a week, the month on the first bar and on the 1st") {
    const auto week = AxisLabels(Days(2026, 10, 19, 7), Granularity::Day, L"en-US");
    CHECK(week.front() == L"Mon 19");
    CHECK(week.back() == L"Sun 25");
    const auto month = AxisLabels(Days(2026, 10, 1, 31), Granularity::Day, L"en-US");
    CHECK(month[0] == L"Oct 1");
    CHECK(month[18] == L"19");
    const auto across = AxisLabels(Days(2026, 10, 20, 30), Granularity::Day, L"en-US");
    CHECK(across[0] == L"Oct 20");
    CHECK(across[11] == L"31");
    CHECK(across[12] == L"Nov 1");
}

TEST_CASE("AxisLabels: months add the year on the first bar and in January once bars span two years") {
    const auto year = AxisLabels(Bars({{2026, 1, 1, 0, 0, 0}, {2026, 2, 1, 0, 0, 0}, {2026, 3, 1, 0, 0, 0}}),
                                 Granularity::Month, L"en-US");
    CHECK(year == std::vector<std::wstring>{L"Jan", L"Feb", L"Mar"});
    const auto span = AxisLabels(Bars({{2025, 11, 1, 0, 0, 0}, {2025, 12, 1, 0, 0, 0}, {2026, 1, 1, 0, 0, 0},
                                       {2026, 2, 1, 0, 0, 0}}),
                                 Granularity::Month, L"en-US");
    CHECK(span == std::vector<std::wstring>{L"Nov 2025", L"Dec", L"Jan 2026", L"Feb"});
}

TEST_CASE("BarTitle names the hour, day or month a bar covers") {
    CHECK(BarTitle(LocalDateTime{2026, 10, 19, 14, 0, 0}, Granularity::Hour, L"en-US") ==
          L"Monday, October 19, 2026, 2:00 PM – 3:00 PM");
    CHECK(BarTitle(LocalDateTime{2026, 10, 19, 23, 0, 0}, Granularity::Hour, L"en-US") ==
          L"Monday, October 19, 2026, 11:00 PM – 12:00 AM");
    CHECK(BarTitle(LocalDateTime{2026, 10, 19, 11, 30, 0}, Granularity::Hour, L"en-US") ==
          L"Monday, October 19, 2026, 11:30 AM – 12:30 PM");
    CHECK(BarTitle(LocalDateTime{2026, 10, 19, 0, 0, 0}, Granularity::Day, L"en-US") == L"Monday, October 19, 2026");
    CHECK(BarTitle(LocalDateTime{2026, 10, 1, 0, 0, 0}, Granularity::Month, L"en-US") == L"October 2026");
}

TEST_CASE("TrimName drops surrounding whitespace so a blank rename restores the Windows name") {
    CHECK(TrimName(L"  Work VPN \t") == L"Work VPN");
    CHECK(TrimName(L"   ").empty());
    CHECK(TrimName(L"").empty());
    CHECK(TrimName(L"a  b") == L"a  b");
}

// ---- The real window, against a temporary database ----

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::random_device rd;
        path = std::filesystem::temp_directory_path() /
               (L"nsm_rptwin_" + std::to_wstring(rd()) + L"_" + std::to_wstring(rd()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

struct TempStore {
    TempDir dir;       // removed last,
    UsageStore store;  // after the store has closed the file
    TempStore() { REQUIRE(store.Open(dir.path / L"usage.db") == UsageStore::OpenResult::Ok); }
    void Add(const wchar_t* adapter, const wchar_t* network, uint64_t rx, uint64_t tx) {
        const int64_t hour = HourStart(static_cast<int64_t>(_time64(nullptr)));
        const UsageRow r{hour, Series::Adapter, adapter, network, rx, tx};
        REQUIRE(store.AddUsage(std::span<const UsageRow>(&r, 1)));
    }
};

HWND FindChild(HWND parent, const wchar_t* cls, int index = 0) {
    HWND w = nullptr;
    for (int i = 0; i <= index; ++i) w = FindWindowExW(parent, w, cls, nullptr);
    return w;
}

std::wstring ItemText(HWND list, int item) {
    wchar_t buf[256] = {};
    ListView_GetItemText(list, item, 0, buf, static_cast<int>(std::size(buf)));
    return buf;
}

}  // namespace

TEST_CASE("ReportWindow fills its tables, follows Show hidden and column sorting, and reopens where it closed" *
          doctest::test_suite("integration")) {
    TempStore t;
    t.store.UpsertAdapter(L"{A}", L"Wi-Fi", AdapterKind::WiFi);
    t.store.UpsertAdapter(L"{B}", L"Ethernet", AdapterKind::Ethernet);
    t.store.UpsertAdapter(L"{C}", L"Hidden VPN", AdapterKind::Virtual);
    t.store.SetHidden(L"{C}", true);
    t.store.UpsertNetwork(NetworkMeta{L"{N1}", L"Home"});
    t.Add(L"{A}", L"{N1}", 5'000'000, 1'000'000);
    t.Add(L"{A}", L"", 1'000, 0);  // Wi-Fi traffic with no network name
    t.Add(L"{B}", L"", 2'000'000, 0);
    t.Add(L"{C}", L"", 9'000'000, 0);

    {
        ReportWindow w(GetModuleHandleW(nullptr), t.store);
        int queries = 0;
        std::vector<RECT> bounds;
        w.beforeQuery = [&] { ++queries; };
        w.onBoundsChanged = [&](RECT r) { bounds.push_back(r); };
        w.SetThemeOverride(ReportTheme::Light);
        w.SetSavedRect(RECT{100, 100, 1000, 800});

        w.Show(2);
        REQUIRE(w.IsOpen());
        CHECK(queries >= 1);  // pending usage is flushed before the first query
        HWND hwnd = FindWindowW(L"NetworkSpeedMonitor.Report", nullptr);
        REQUIRE(hwnd);
        RECT opened{};
        GetWindowRect(hwnd, &opened);
        CHECK(Same(opened, RECT{100, 100, 1000, 800}));

        HWND adapters = FindChild(hwnd, WC_LISTVIEWW, 0);
        HWND wifi = FindChild(hwnd, WC_LISTVIEWW, 1);
        REQUIRE(adapters);
        REQUIRE(wifi);
        CHECK(ListView_GetItemCount(adapters) == 2);  // the hidden VPN is left out
        CHECK(ItemText(adapters, 0) == L"Wi-Fi");     // largest total first
        CHECK(ListView_GetItemCount(wifi) == 2);
        CHECK(ItemText(wifi, 0) == L"Home");
        CHECK(ItemText(wifi, 1) == LoadStr(IDS_UNKNOWN_WIFI));

        HWND showHidden = FindWindowExW(hwnd, nullptr, WC_BUTTONW, LoadStr(IDS_SHOW_HIDDEN).c_str());
        REQUIRE(showHidden);
        SendMessageW(showHidden, BM_CLICK, 0, 0);
        CHECK(ListView_GetItemCount(adapters) == 3);
        CHECK(ItemText(adapters, 0) == FillPlaceholders(LoadStr(IDS_ADAPTER_HIDDEN), {L"Hidden VPN"}));

        NMLISTVIEW click{};
        click.hdr.hwndFrom = adapters;
        click.hdr.idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(adapters));
        click.hdr.code = LVN_COLUMNCLICK;
        click.iSubItem = 0;  // Name: A to Z
        SendMessageW(hwnd, WM_NOTIFY, click.hdr.idFrom, reinterpret_cast<LPARAM>(&click));
        CHECK(ItemText(adapters, 0) == L"Ethernet");
        SendMessageW(hwnd, WM_NOTIFY, click.hdr.idFrom, reinterpret_cast<LPARAM>(&click));  // Z to A
        CHECK(ItemText(adapters, 0) == L"Wi-Fi");

        // Moved, then closed: the new bounds are reported and the window is gone.
        SetWindowPos(hwnd, nullptr, 150, 120, 850, 650, SWP_NOZORDER | SWP_NOACTIVATE);
        w.Close();
        CHECK_FALSE(w.IsOpen());
        CHECK_FALSE(IsWindow(hwnd));
        REQUIRE_FALSE(bounds.empty());
        CHECK(Same(bounds.back(), RECT{150, 120, 1000, 770}));

        // Reopening creates it again where it was.
        w.Show(0);
        REQUIRE(w.IsOpen());
        hwnd = FindWindowW(L"NetworkSpeedMonitor.Report", nullptr);
        RECT reopened{};
        GetWindowRect(hwnd, &reopened);
        CHECK(Same(reopened, RECT{150, 120, 1000, 770}));
        w.Close();
    }
}
