#include "doctest.h"

#include <windows.h>
#include <commctrl.h>

#include <ctime>
#include <filesystem>
#include <random>
#include <span>
#include <utility>
#include <vector>

#include "core/periods.h"
#include "platform/strings.h"
#include "platform/theme.h"
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
    // The short bucket after a part-hour transition (Lord Howe: 02:00 -> 02:30) keeps its minutes.
    const auto lordHowe =
        AxisLabels(Bars({{2026, 10, 4, 1, 0, 0}, {2026, 10, 4, 2, 30, 0}}), Granularity::Hour, L"en-US");
    CHECK(lordHowe == std::vector<std::wstring>{L"1 AM", L"2:30 AM"});
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

TEST_CASE("AxisLabels' date pictures come from the STRINGTABLE, so a translation can reorder them") {
    CHECK(LoadStr(IDS_AXIS_WEEKDAY) == L"ddd d");
    CHECK(LoadStr(IDS_AXIS_MONTH_DAY) == L"MMM d");
    CHECK(LoadStr(IDS_AXIS_DAY) == L"d");
    CHECK(LoadStr(IDS_AXIS_MONTH_YEAR) == L"MMM yyyy");
    CHECK(LoadStr(IDS_AXIS_MONTH) == L"MMM");
}

namespace {
TimelineBar Bar(LocalDateTime start, int64_t lengthSec = 0) { return TimelineBar{start, {}, lengthSec}; }
}  // namespace

TEST_CASE("BarTitle names the hour, day or month a bar covers") {
    CHECK(BarTitle(Bar({2026, 10, 19, 14, 0, 0}, 3600), Granularity::Hour, L"en-US") ==
          L"Monday, October 19, 2026, 2:00 PM – 3:00 PM");
    CHECK(BarTitle(Bar({2026, 10, 19, 23, 0, 0}, 3600), Granularity::Hour, L"en-US") ==
          L"Monday, October 19, 2026, 11:00 PM – 12:00 AM");
    // A short bucket after a part-hour transition (Lord Howe: 02:00 -> 02:30) ends on the next full hour.
    CHECK(BarTitle(Bar({2026, 10, 4, 2, 30, 0}, 1800), Granularity::Hour, L"en-US") ==
          L"Sunday, October 4, 2026, 2:30 AM – 3:00 AM");
    // Without a length, the next full hour.
    CHECK(BarTitle(Bar({2026, 10, 4, 2, 30, 0}), Granularity::Hour, L"en-US") ==
          L"Sunday, October 4, 2026, 2:30 AM – 3:00 AM");
    CHECK(BarTitle(Bar({2026, 10, 19, 0, 0, 0}), Granularity::Day, L"en-US") == L"Monday, October 19, 2026");
    CHECK(BarTitle(Bar({2026, 10, 1, 0, 0, 0}), Granularity::Month, L"en-US") == L"October 2026");
}

TEST_CASE("BarTitle ends the hour cut short by Chatham's 02:45 DST change at 02:45") {
    // 2026-09-27 02:45 CHAST -> 03:45 CHADT: the 02:00 bucket lasts 45 minutes, the 03:45 one 15.
    CHECK(BarTitle(Bar({2026, 9, 27, 2, 0, 0}, 2700), Granularity::Hour, L"en-US") ==
          L"Sunday, September 27, 2026, 2:00 AM – 2:45 AM");
    CHECK(BarTitle(Bar({2026, 9, 27, 3, 45, 0}, 900), Granularity::Hour, L"en-US") ==
          L"Sunday, September 27, 2026, 3:45 AM – 4:00 AM");
    // A bucket that ends at a fall-back change (03:45 CHADT -> 02:45 CHAST) ends on the clock it started on.
    CHECK(BarTitle(Bar({2027, 4, 4, 3, 0, 0}, 2700), Granularity::Hour, L"en-US") ==
          L"Sunday, April 4, 2027, 3:00 AM – 3:45 AM");
}

TEST_CASE("SummaryNotices shows the unhealthy banner exactly when the store isn't healthy") {
    CHECK(SummaryNotices({}, true).empty());
    CHECK(SummaryNotices({}, false) == std::vector<std::wstring>{LoadStr(IDS_NOTE_UNHEALTHY)});
    const std::filesystem::path recovered = L"C:\\data\\usage.db.corrupt";
    const auto both = SummaryNotices(recovered, false);
    REQUIRE(both.size() == 2);
    CHECK(both[0] == FillPlaceholders(LoadStr(IDS_NOTE_CORRUPT), {recovered.wstring()}));
    CHECK(both[1] == LoadStr(IDS_NOTE_UNHEALTHY));
    CHECK(SummaryNotices(recovered, true).size() == 1);
}

TEST_CASE("SetDarkTitleBar falls back to attribute 19 on Windows 10 1809-1909") {
    std::vector<std::pair<DWORD, BOOL>> calls;
    auto setter = [&](HRESULT result20, HRESULT result19) {
        return [&calls, result20, result19](HWND, DWORD attribute, const void* value, DWORD size) {
            REQUIRE(size == sizeof(BOOL));
            calls.emplace_back(attribute, *static_cast<const BOOL*>(value));
            return attribute == 20 ? result20 : result19;
        };
    };
    CHECK(SetDarkTitleBar(nullptr, true, setter(S_OK, E_INVALIDARG)));  // 20H1 and later: 20 only
    CHECK(calls == std::vector<std::pair<DWORD, BOOL>>{{20, TRUE}});
    calls.clear();
    CHECK(SetDarkTitleBar(nullptr, true, setter(E_INVALIDARG, S_OK)));  // 1809-1909: 20 is refused
    CHECK(calls == std::vector<std::pair<DWORD, BOOL>>{{20, TRUE}, {19, TRUE}});
    calls.clear();
    CHECK(SetDarkTitleBar(nullptr, false, setter(E_INVALIDARG, S_OK)));  // switching back to light
    CHECK(calls == std::vector<std::pair<DWORD, BOOL>>{{20, FALSE}, {19, FALSE}});
    calls.clear();
    CHECK_FALSE(SetDarkTitleBar(nullptr, true, setter(E_INVALIDARG, E_INVALIDARG)));  // before 1809
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
        const int64_t hour = LocalHourStart(static_cast<int64_t>(_time64(nullptr)), TimeZone::Local());
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

TEST_CASE("IST: Today's timeline labels start at 12 AM" * doctest::test_suite("integration")) {
    TempStore t;
    const auto ist = *TimeZone::FromKey(L"India Standard Time");
    const int64_t now = 1792348800;   // 2026-10-19 00:10 IST (18:40Z on Oct 18)
    const UsageRow r{LocalHourStart(now, ist), Series::Internet, L"{A}", L"", 5, 5};
    REQUIRE(t.store.AddUsage(std::span<const UsageRow>(&r, 1)));
    ReportModel m(t.store, ist, 0);
    const auto bars = m.Timeline(ResolvePeriod(Period::Today, now, ist, 0), now);
    REQUIRE(bars.size() == 24);
    const auto labels = AxisLabels(bars, Granularity::Hour, L"en-US");
    CHECK(labels.front() == L"12 AM");
    CHECK(labels.back() == L"11 PM");
    CHECK(BarTitle(bars.front(), Granularity::Hour, L"en-US") == L"Monday, October 19, 2026, 12:00 AM – 1:00 AM");
    CHECK(bars.front().totals.Total() == 10);
}

namespace {

// The id of the menu item labelled with the string `textId`; 0 if there is none.
UINT MenuCommand(HMENU menu, UINT textId) {
    const std::wstring want = LoadStr(textId);
    for (int i = 0; i < GetMenuItemCount(menu); ++i) {
        wchar_t text[128] = {};
        GetMenuStringW(menu, static_cast<UINT>(i), text, static_cast<int>(std::size(text)), MF_BYPOSITION);
        if (!want.empty() && want == text) return GetMenuItemID(menu, i);
    }
    return 0;
}

// Opens the context menu of a table's first row, the way Shift+F10 does.
void OpenFirstRowMenu(HWND report, HWND list) {
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(report, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(list), static_cast<LPARAM>(-1));
}

// Picks a period as a click in the open list does. The report handles it from a posted message.
void PickPeriod(HWND report, HWND combo, int index) {
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    SendMessageW(report, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(combo), CBN_SELENDOK), reinterpret_cast<LPARAM>(combo));
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

}  // namespace

TEST_CASE("ReportWindow tells the app about renames and hiding, and rebuilds its tables for a new period" *
          doctest::test_suite("integration")) {
    TempStore t;
    t.store.UpsertAdapter(L"{A}", L"Wi-Fi", AdapterKind::WiFi);
    t.store.UpsertAdapter(L"{B}", L"Ethernet", AdapterKind::Ethernet);
    t.store.UpsertNetwork(NetworkMeta{L"{N1}", L"Home"});
    t.Add(L"{A}", L"{N1}", 5'000'000, 0);
    t.Add(L"{B}", L"", 2'000'000, 0);

    {
        ReportWindow w(GetModuleHandleW(nullptr), t.store);
        w.SetThemeOverride(ReportTheme::Light);
        int changes = 0;
        w.onAdaptersChanged = [&] { ++changes; };
        UINT pick = 0;  // the string id of the row-menu item to choose
        w.pickRowCommand = [&](HMENU menu) { return MenuCommand(menu, pick); };
        std::optional<std::wstring> newName;  // what the Rename dialog returns
        w.askNewName = [&](const std::wstring&) { return newName; };

        w.Show(2);
        REQUIRE(w.IsOpen());
        HWND hwnd = FindWindowW(L"NetworkSpeedMonitor.Report", nullptr);
        REQUIRE(hwnd);
        HWND adapters = FindChild(hwnd, WC_LISTVIEWW, 0);
        HWND wifi = FindChild(hwnd, WC_LISTVIEWW, 1);
        HWND period = FindChild(hwnd, WC_COMBOBOXW);
        REQUIRE(adapters);
        REQUIRE(wifi);
        REQUIRE(period);
        REQUIRE(ListView_GetItemCount(adapters) == 2);
        REQUIRE(ItemText(adapters, 0) == L"Wi-Fi");

        pick = IDS_MENU_RENAME;
        newName = L"Laptop Wi-Fi";
        OpenFirstRowMenu(hwnd, adapters);
        CHECK(changes == 1);
        CHECK(ItemText(adapters, 0) == L"Laptop Wi-Fi");
        newName.reset();  // cancelled: nothing is stored, so there is nothing to tell
        OpenFirstRowMenu(hwnd, adapters);
        CHECK(changes == 1);

        pick = IDS_MENU_HIDE;
        OpenFirstRowMenu(hwnd, adapters);
        CHECK(changes == 2);
        CHECK(ListView_GetItemCount(adapters) == 1);
        CHECK(ItemText(adapters, 0) == L"Ethernet");

        // All the traffic is from this hour: none yesterday, all of it today. Hidden adapters still count
        // in the Wi-Fi networks table.
        PickPeriod(hwnd, period, 1);  // Yesterday
        CHECK(ListView_GetItemCount(adapters) == 0);
        CHECK(ListView_GetItemCount(wifi) == 0);
        PickPeriod(hwnd, period, 0);  // Today
        CHECK(ListView_GetItemCount(adapters) == 1);
        CHECK(ListView_GetItemCount(wifi) == 1);
        CHECK(changes == 2);
        w.Close();
    }
}

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
        // Place the window inside this machine's work area: CI runners have small (1024x768) screens and the
        // report clamps a saved rect that doesn't fit, which is correct but would break exact-rect checks.
        RECT wa{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        const RECT saved{wa.left + 20, wa.top + 20, wa.left + 20 + 820, wa.top + 20 + 620};
        const RECT moved{wa.left + 40, wa.top + 30, wa.left + 40 + 800, wa.top + 30 + 600};
        const bool fits = wa.right - wa.left >= 880 && wa.bottom - wa.top >= 680;
        if (!fits) MESSAGE("work area too small for exact-rect checks; skipping them");
        w.SetSavedRect(saved);

        w.Show(2);
        REQUIRE(w.IsOpen());
        CHECK(queries >= 1);  // pending usage is flushed before the first query
        HWND hwnd = FindWindowW(L"NetworkSpeedMonitor.Report", nullptr);
        REQUIRE(hwnd);
        RECT opened{};
        GetWindowRect(hwnd, &opened);
        if (fits) CHECK(Same(opened, saved));

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
        SetWindowPos(hwnd, nullptr, moved.left, moved.top, moved.right - moved.left, moved.bottom - moved.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        RECT movedActual{};
        GetWindowRect(hwnd, &movedActual);  // the window's minimum size may enlarge it
        w.Close();
        CHECK_FALSE(w.IsOpen());
        CHECK_FALSE(IsWindow(hwnd));
        REQUIRE_FALSE(bounds.empty());
        CHECK(Same(bounds.back(), movedActual));

        // Reopening creates it again where it was.
        w.Show(0);
        REQUIRE(w.IsOpen());
        hwnd = FindWindowW(L"NetworkSpeedMonitor.Report", nullptr);
        RECT reopened{};
        GetWindowRect(hwnd, &reopened);
        if (fits) CHECK(Same(reopened, movedActual));
        w.Close();
    }
}
