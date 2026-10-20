#include "ui/report_window.h"

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "core/units.h"
#include "platform/strings.h"
#include "platform/theme.h"
#include "report/csv_export.h"
#include "resource.h"
#include "ui/dialogs.h"

namespace nsm {

struct ReportWindow::TableState {
    HWND list = nullptr;
    bool adapters = false;  // else the Wi-Fi networks table
    std::vector<TableColumn> columns;
    TableSort sort;
    std::vector<TableRow> rows;  // in display order
};

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kWindowClass[] = L"NetworkSpeedMonitor.Report";
constexpr wchar_t kCanvasClass[] = L"NetworkSpeedMonitor.ReportCanvas";
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kRefreshMs = 60'000;
constexpr UINT WM_APP_PERIOD = WM_APP + 1;      // private to the report window: a period was picked
constexpr DWORD kDwmUseImmersiveDarkMode = 20;  // DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 20H1+)

enum ControlId : int {
    kIdPeriodLabel = 100,
    kIdPeriod,
    kIdRange,
    kIdExport,
    kIdClearAll,
    kIdTabs,
    kIdSummary,
    kIdTimeline,
    kIdAdaptersNote,
    kIdShowHidden,
    kIdAdapters,
    kIdWifiNote,
    kIdWifi,
};
enum MenuCommand : UINT { kCmdRename = 1, kCmdHide, kCmdClearAdapter, kCmdClearNetwork };

constexpr int kTabCount = 4;
constexpr UINT kTabNames[kTabCount] = {IDS_TAB_SUMMARY, IDS_TAB_TIMELINE, IDS_TAB_ADAPTERS, IDS_TAB_WIFI};

struct PeriodChoice {
    Period period;
    UINT name;
};
constexpr PeriodChoice kPeriods[] = {
    {Period::Today, IDS_PERIOD_TODAY},          {Period::Yesterday, IDS_PERIOD_YESTERDAY},
    {Period::ThisWeek, IDS_PERIOD_THIS_WEEK},   {Period::LastWeek, IDS_PERIOD_LAST_WEEK},
    {Period::ThisMonth, IDS_PERIOD_THIS_MONTH}, {Period::LastMonth, IDS_PERIOD_LAST_MONTH},
    {Period::Last30Days, IDS_PERIOD_LAST_30_DAYS}, {Period::ThisYear, IDS_PERIOD_THIS_YEAR},
    {Period::AllTime, IDS_PERIOD_ALL_TIME},     {Period::Custom, IDS_PERIOD_CUSTOM},
};
constexpr int kDefaultPeriod = 4;  // This month
constexpr int kCustomPeriod = 9;

// Layout in DIPs.
constexpr int kMargin = 12;
constexpr int kGap = 8;
constexpr int kComboWidth = 160;
constexpr int kButtonPadding = 28;
constexpr int kTypeColumn = 90;
constexpr int kNumberColumn = 100;
constexpr SIZE kDefaultSize{800, 600};
constexpr SIZE kMinSize{700, 520};

int64_t NowUtc() { return static_cast<int64_t>(_time64(nullptr)); }

UINT ColumnName(TableColumn c) {
    switch (c) {
        case TableColumn::Name: return IDS_COL_NAME;
        case TableColumn::Type: return IDS_COL_TYPE;
        case TableColumn::Received: return IDS_COL_RECEIVED;
        case TableColumn::Sent: return IDS_COL_SENT;
        case TableColumn::Total:
        default: return IDS_COL_TOTAL;
    }
}

bool IsNumeric(TableColumn c) {
    return c == TableColumn::Received || c == TableColumn::Sent || c == TableColumn::Total;
}

std::wstring KindName(AdapterKind k) {
    switch (k) {
        case AdapterKind::WiFi: return LoadStr(IDS_KIND_WIFI);
        case AdapterKind::Ethernet: return LoadStr(IDS_KIND_ETHERNET);
        case AdapterKind::Virtual: return LoadStr(IDS_KIND_VIRTUAL);
        case AdapterKind::Other:
        default: return LoadStr(IDS_KIND_OTHER);
    }
}

int CompareText(const std::wstring& a, const std::wstring& b) {
    const int c = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a.c_str(),
                                  static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), nullptr, nullptr,
                                  0);
    if (c == CSTR_LESS_THAN) return -1;
    if (c == CSTR_GREATER_THAN) return 1;
    return 0;
}

template <class T>
int Compare(T a, T b) {
    return a < b ? -1 : b < a ? 1 : 0;
}

SYSTEMTIME SystemTimeOf(int year, int month, int day, int hour = 0, int minute = 0) {
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(year);
    st.wMonth = static_cast<WORD>(month);
    st.wDay = static_cast<WORD>(day);
    st.wHour = static_cast<WORD>(hour);
    st.wMinute = static_cast<WORD>(minute);
    return st;
}

// An explicit locale means that locale's defaults; nullptr means the user's locale with their overrides.
std::wstring DateText(const SYSTEMTIME& st, DWORD flags, const wchar_t* picture, const wchar_t* locale) {
    wchar_t buf[128];
    const DWORD f = picture ? 0 : flags | (locale ? LOCALE_NOUSEROVERRIDE : 0);
    const int n = GetDateFormatEx(locale ? locale : LOCALE_NAME_USER_DEFAULT, f, &st, picture, buf,
                                  static_cast<int>(std::size(buf)), nullptr);
    return n > 0 ? std::wstring(buf, static_cast<size_t>(n - 1)) : std::wstring{};
}

std::wstring TimeText(const SYSTEMTIME& st, DWORD flags, const wchar_t* locale) {
    wchar_t buf[64];
    const DWORD f = flags | (locale ? LOCALE_NOUSEROVERRIDE : 0);
    const int n = GetTimeFormatEx(locale ? locale : LOCALE_NAME_USER_DEFAULT, f, &st, nullptr, buf,
                                  static_cast<int>(std::size(buf)));
    return n > 0 ? std::wstring(buf, static_cast<size_t>(n - 1)) : std::wstring{};
}

SIZE TextExtent(HWND ctl, HFONT font, const std::wstring& s, UINT format = DT_SINGLELINE, int width = 0) {
    HDC dc = GetDC(ctl);
    HGDIOBJ old = SelectObject(dc, font);
    RECT r{0, 0, width, 0};
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, format | DT_CALCRECT);
    SelectObject(dc, old);
    ReleaseDC(ctl, dc);
    return SIZE{r.right - r.left, r.bottom - r.top};
}

}  // namespace

// ---- Pure rules ----

RECT FitReportRect(const std::optional<RECT>& saved, const RECT& work, SIZE defaultSize, SIZE minSize) {
    const LONG workW = work.right - work.left, workH = work.bottom - work.top;
    if (!saved) {
        const LONG w = std::min(defaultSize.cx, workW), h = std::min(defaultSize.cy, workH);
        const LONG left = work.left + (workW - w) / 2, top = work.top + (workH - h) / 2;
        return RECT{left, top, left + w, top + h};
    }
    const LONG w = std::clamp(saved->right - saved->left, std::min(minSize.cx, workW), workW);
    const LONG h = std::clamp(saved->bottom - saved->top, std::min(minSize.cy, workH), workH);
    const LONG left = std::clamp(saved->left, work.left, work.right - w);
    const LONG top = std::clamp(saved->top, work.top, work.bottom - h);
    return RECT{left, top, left + w, top + h};
}

TableSort NextSort(TableSort current, TableColumn clicked) {
    if (current.column == clicked) return TableSort{clicked, !current.ascending};
    return TableSort{clicked, !IsNumeric(clicked)};
}

void SortRows(std::vector<TableRow>& rows, TableSort sort) {
    auto primary = [&](const TableRow& a, const TableRow& b) {
        switch (sort.column) {
            case TableColumn::Name: return CompareText(a.name, b.name);
            case TableColumn::Type: return CompareText(a.type, b.type);
            case TableColumn::Received: return Compare(a.data.totals.rx, b.data.totals.rx);
            case TableColumn::Sent: return Compare(a.data.totals.tx, b.data.totals.tx);
            case TableColumn::Total:
            default: return Compare(a.data.totals.Total(), b.data.totals.Total());
        }
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const TableRow& a, const TableRow& b) {
        if (const int c = primary(a, b); c != 0) return sort.ascending ? c < 0 : c > 0;
        if (a.data.totals.Total() != b.data.totals.Total()) return a.data.totals.Total() > b.data.totals.Total();
        if (const int c = CompareText(a.name, b.name); c != 0) return c < 0;
        return a.data.id < b.data.id;
    });
}

std::wstring DefaultCsvName(LocalDate today) {
    wchar_t buf[64];
    swprintf(buf, std::size(buf), L"network-usage-%04d-%02d-%02d.csv", today.year, today.month, today.day);
    return buf;
}

int FixedSummaryCard(Period p) {
    switch (p) {
        case Period::Today: return 0;
        case Period::ThisWeek: return 1;
        case Period::ThisMonth: return 2;
        default: return -1;
    }
}

std::wstring FillPlaceholders(const std::wstring& format, std::initializer_list<std::wstring> args) {
    std::wstring out;
    out.reserve(format.size());
    auto next = args.begin();
    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] == L'%' && i + 1 < format.size() && format[i + 1] == L's') {
            if (next != args.end()) out += *next++;
            ++i;
        } else {
            out += format[i];
        }
    }
    return out;
}

std::wstring RangeLabel(LocalDate first, LocalDate last, const wchar_t* locale) {
    const std::wstring a = DateText(SystemTimeOf(first.year, first.month, first.day), DATE_SHORTDATE, nullptr, locale);
    if (first == last) return a;
    const std::wstring b = DateText(SystemTimeOf(last.year, last.month, last.day), DATE_SHORTDATE, nullptr, locale);
    return FillPlaceholders(LoadStr(IDS_RANGE_FORMAT), {a, b});
}

std::vector<std::wstring> AxisLabels(const std::vector<TimelineBar>& bars, Granularity g, const wchar_t* locale) {
    std::vector<std::wstring> out;
    out.reserve(bars.size());
    const bool spansYears = !bars.empty() && bars.front().start.year != bars.back().start.year;
    for (size_t i = 0; i < bars.size(); ++i) {
        const LocalDateTime& t = bars[i].start;
        const SYSTEMTIME st = SystemTimeOf(t.year, t.month, t.day, t.hour, t.minute);
        switch (g) {
            case Granularity::Hour:
                out.push_back(TimeText(st, t.minute == 0 ? TIME_NOMINUTESORSECONDS : TIME_NOSECONDS, locale));
                break;
            case Granularity::Day:
                if (bars.size() <= 8) out.push_back(DateText(st, 0, L"ddd d", locale));
                else out.push_back(DateText(st, 0, i == 0 || t.day == 1 ? L"MMM d" : L"d", locale));
                break;
            case Granularity::Month:
            default:
                out.push_back(DateText(st, 0, spansYears && (i == 0 || t.month == 1) ? L"MMM yyyy" : L"MMM", locale));
                break;
        }
    }
    return out;
}

std::wstring BarTitle(const LocalDateTime& start, Granularity g, const wchar_t* locale) {
    const SYSTEMTIME st = SystemTimeOf(start.year, start.month, start.day, start.hour, start.minute);
    switch (g) {
        case Granularity::Hour: {
            // Buckets end on the next full local hour (a short one after a part-hour DST change starts
            // at :30 or :45). Only the time of the end is shown, so the date it falls on doesn't matter.
            const SYSTEMTIME end = SystemTimeOf(start.year, start.month, start.day, (start.hour + 1) % 24, 0);
            return FillPlaceholders(LoadStr(IDS_BAR_HOUR), {DateText(st, DATE_LONGDATE, nullptr, locale),
                                                            TimeText(st, TIME_NOSECONDS, locale),
                                                            TimeText(end, TIME_NOSECONDS, locale)});
        }
        case Granularity::Day: return DateText(st, DATE_LONGDATE, nullptr, locale);
        case Granularity::Month:
        default: return DateText(st, DATE_YEARMONTH, nullptr, locale);
    }
}

std::wstring LongDate(LocalDate d, const wchar_t* locale) {
    return DateText(SystemTimeOf(d.year, d.month, d.day), DATE_LONGDATE, nullptr, locale);
}

// ---- ReportWindow ----

ReportWindow::ReportWindow(HINSTANCE inst, UsageStore& store, HWND owner)
    : inst_(inst),
      store_(store),
      owner_(owner),
      periodIndex_(kDefaultPeriod),
      adapters_(std::make_unique<TableState>()),
      wifi_(std::make_unique<TableState>()) {
    adapters_->adapters = true;
    adapters_->columns = {TableColumn::Name, TableColumn::Type, TableColumn::Received, TableColumn::Sent,
                          TableColumn::Total};
    wifi_->columns = {TableColumn::Name, TableColumn::Received, TableColumn::Sent, TableColumn::Total};
}

ReportWindow::~ReportWindow() { Close(); }

void ReportWindow::Close() {
    if (hwnd_) DestroyWindow(hwnd_);
}

void ReportWindow::SetCorruptionNotice(std::filesystem::path recoveredFile) { corruptFile_ = std::move(recoveredFile); }

void ReportWindow::SetSavedRect(std::optional<RECT> rect) { savedRect_ = rect; }

void ReportWindow::SetThemeOverride(std::optional<ReportTheme> theme) { themeOverride_ = theme; }

int ReportWindow::Scale(int dips) const { return MulDiv(dips, static_cast<int>(dpi_), 96); }

void ReportWindow::Show(int tab) {
    if (!hwnd_ && !Create()) return;
    SelectTab(std::clamp(tab, 0, kTabCount - 1));
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd_);
}

bool ReportWindow::Create() {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES |
                                              ICC_BAR_CLASSES | ICC_DATE_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &ReportWindow::WndProc;
    wc.hInstance = inst_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    WNDCLASSEXW cc{sizeof(cc)};
    cc.lpfnWndProc = &ReportWindow::CanvasProc;
    cc.hInstance = inst_;
    cc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cc.lpszClassName = kCanvasClass;
    if (!RegisterClassExW(&cc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.ReleaseAndGetAddressOf()))) return false;
    painter_ = std::make_unique<ReportPainter>();

    // Open where it was last, on a monitor that still exists; otherwise centred where the user clicked.
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = savedRect_ ? MonitorFromRect(&*savedRect_, MONITOR_DEFAULTTONEAREST)
                                  : MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
    auto px = [&](LONG dips) { return static_cast<LONG>(MulDiv(dips, static_cast<int>(dpiX), 96)); };
    const RECT r = FitReportRect(savedRect_, mi.rcWork, SIZE{px(kDefaultSize.cx), px(kDefaultSize.cy)},
                                 SIZE{px(kMinSize.cx), px(kMinSize.cy)});
    // WS_EX_APPWINDOW: a taskbar button even though the window is owned.
    CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, LoadStr(IDS_REPORT_TITLE).c_str(),
                    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, r.left, r.top, r.right - r.left, r.bottom - r.top, owner_,
                    nullptr, inst_, this);
    if (!hwnd_) {
        painter_.reset();
        d2d_.Reset();
        return false;
    }
    return true;
}

void ReportWindow::CreateControls() {
    auto make = [this](const wchar_t* cls, const std::wstring& text, DWORD style, int id) {
        return CreateWindowExW(0, cls, text.c_str(), WS_CHILD | style, 0, 0, 0, 0, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst_, nullptr);
    };
    // Creation order is the Tab order.
    periodLabel_ = make(WC_STATICW, LoadStr(IDS_PERIOD_LABEL), WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, kIdPeriodLabel);
    period_ = make(WC_COMBOBOXW, L"", WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, kIdPeriod);
    for (const auto& p : kPeriods) ComboBox_AddString(period_, LoadStr(p.name).c_str());
    ComboBox_SetCurSel(period_, periodIndex_);
    range_ = make(WC_STATICW, L"", WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX | SS_ENDELLIPSIS, kIdRange);
    export_ = make(WC_BUTTONW, LoadStr(IDS_BTN_EXPORT), WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, kIdExport);
    clearAll_ = make(WC_BUTTONW, LoadStr(IDS_BTN_CLEAR_ALL), WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, kIdClearAll);

    tabs_ = make(WC_TABCONTROLW, L"", WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, kIdTabs);
    for (int i = 0; i < kTabCount; ++i) {
        std::wstring name = LoadStr(kTabNames[i]);
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = name.data();
        TabCtrl_InsertItem(tabs_, i, &item);
    }
    SetWindowSubclass(tabs_, &ReportWindow::TabsSubclass, 1, reinterpret_cast<DWORD_PTR>(this));

    summary_ = make(kCanvasClass, L"", 0, kIdSummary);
    timeline_ = make(kCanvasClass, L"", WS_TABSTOP, kIdTimeline);
    adaptersNote_ = make(WC_STATICW, LoadStr(IDS_NOTE_RAW), SS_LEFT | SS_NOPREFIX, kIdAdaptersNote);
    showHiddenBox_ = make(WC_BUTTONW, LoadStr(IDS_SHOW_HIDDEN), WS_TABSTOP | BS_AUTOCHECKBOX, kIdShowHidden);
    Button_SetCheck(showHiddenBox_, showHiddenRows_ ? BST_CHECKED : BST_UNCHECKED);
    adapters_->list = make(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, kIdAdapters);
    wifiNote_ = make(WC_STATICW, LoadStr(IDS_NOTE_RAW), SS_LEFT | SS_NOPREFIX, kIdWifiNote);
    wifi_->list = make(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, kIdWifi);
    for (TableState* t : {adapters_.get(), wifi_.get()}) {
        ListView_SetExtendedListViewStyle(t->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        for (size_t c = 0; c < t->columns.size(); ++c) {
            std::wstring title = LoadStr(ColumnName(t->columns[c]));
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_FMT | LVCF_SUBITEM;
            col.fmt = IsNumeric(t->columns[c]) ? LVCFMT_RIGHT : LVCFMT_LEFT;
            col.pszText = title.data();
            col.iSubItem = static_cast<int>(c);
            ListView_InsertColumn(t->list, static_cast<int>(c), &col);
        }
        SetWindowSubclass(t->list, &ReportWindow::ListSubclass, 1, reinterpret_cast<DWORD_PTR>(this));
    }

    // Exact values for the bar under the pointer (or the keyboard-selected bar).
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd_, nullptr, inst_,
                               nullptr);
    TTTOOLINFOW ti{sizeof(ti)};
    ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    ti.hwnd = timeline_;
    ti.uId = 1;
    ti.lpszText = const_cast<wchar_t*>(L"");
    SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

void ReportWindow::ApplyFont() {
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi_);
    HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);
    for (HWND c = GetWindow(hwnd_, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    }
    if (tooltip_) {
        SendMessageW(tooltip_, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, Scale(400));
    }
    if (font_) DeleteObject(font_);
    font_ = font;
}

ReportTheme ReportWindow::CurrentTheme() const {
    if (themeOverride_) return *themeOverride_;
    if (IsHighContrast()) return ReportTheme::HighContrast;
    return AreAppsLight() ? ReportTheme::Light : ReportTheme::Dark;
}

void ReportWindow::ApplyTheme() {
    theme_ = CurrentTheme();
    palette_ = PaletteFor(theme_);
    if (backgroundBrush_) DeleteObject(backgroundBrush_);
    backgroundBrush_ = CreateSolidBrush(palette_.background);

    const bool dark = theme_ == ReportTheme::Dark;
    BOOL useDark = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &useDark, sizeof(useDark));
    const wchar_t* controls = dark ? L"DarkMode_Explorer" : nullptr;
    for (HWND w : {export_, clearAll_, showHiddenBox_, tooltip_}) SetWindowTheme(w, controls, nullptr);
    SetWindowTheme(period_, dark ? L"DarkMode_CFD" : nullptr, nullptr);
    for (TableState* t : {adapters_.get(), wifi_.get()}) {
        const wchar_t* listTheme =
            theme_ == ReportTheme::HighContrast ? nullptr : dark ? L"DarkMode_Explorer" : L"Explorer";
        SetWindowTheme(t->list, listTheme, nullptr);
        SetWindowTheme(ListView_GetHeader(t->list), dark ? L"DarkMode_ItemsView" : nullptr, nullptr);
        ListView_SetBkColor(t->list, palette_.background);
        ListView_SetTextBkColor(t->list, palette_.background);
        ListView_SetTextColor(t->list, palette_.text);
    }
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

void ReportWindow::Layout() {
    if (!tabs_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int m = Scale(kMargin), gap = Scale(kGap);

    // Top row: Period [combo] range ........ [Export CSV…] [Clear all history…]
    RECT combo;
    GetWindowRect(period_, &combo);  // a drop-down list's closed height follows its font
    const int rowH = std::max<int>(combo.bottom - combo.top, Scale(24));
    int x = m;
    const int y = m;
    const int labelW = TextExtent(periodLabel_, font_, LoadStr(IDS_PERIOD_LABEL)).cx;
    MoveWindow(periodLabel_, x, y, labelW, rowH, TRUE);
    x += labelW + Scale(6);
    MoveWindow(period_, x, y, Scale(kComboWidth), Scale(320), TRUE);  // height includes the drop-down list
    x += Scale(kComboWidth) + gap;
    int right = rc.right - m;
    for (const auto& [button, text] : {std::pair{clearAll_, IDS_BTN_CLEAR_ALL}, std::pair{export_, IDS_BTN_EXPORT}}) {
        const int w = TextExtent(button, font_, LoadStr(text)).cx + Scale(kButtonPadding);
        MoveWindow(button, right - w, y, w, rowH, TRUE);
        right -= w + gap;
    }
    MoveWindow(range_, x, y, std::max(0, right - x), rowH, TRUE);

    // The tab control is only the strip of tabs; each page sits below it.
    const int tabsTop = y + rowH + gap;
    const int width = std::max(0, static_cast<int>(rc.right) - 2 * m);
    MoveWindow(tabs_, m, tabsTop, width, Scale(40), FALSE);
    RECT item{};
    TabCtrl_GetItemRect(tabs_, 0, &item);
    const int stripH = item.bottom + Scale(1);
    MoveWindow(tabs_, m, tabsTop, width, stripH, TRUE);
    pageTop_ = tabsTop + stripH + gap;
    const RECT page{m, pageTop_, rc.right - m, rc.bottom - m};
    const int pw = std::max(0, static_cast<int>(page.right - page.left));
    const int ph = std::max(0, static_cast<int>(page.bottom - page.top));
    pageWidth_ = pw;
    MoveWindow(summary_, page.left, page.top, pw, ph, TRUE);
    MoveWindow(timeline_, page.left, page.top, pw, ph, TRUE);

    for (TableState* t : {adapters_.get(), wifi_.get()}) {
        HWND note = t->adapters ? adaptersNote_ : wifiNote_;
        const int noteH = TextExtent(note, font_, LoadStr(IDS_NOTE_RAW), DT_WORDBREAK | DT_NOPREFIX, pw).cy;
        MoveWindow(note, page.left, page.top, pw, noteH, TRUE);
        int top = page.top + noteH + gap;
        if (t->adapters) {
            const int boxW = TextExtent(showHiddenBox_, font_, LoadStr(IDS_SHOW_HIDDEN)).cx + Scale(28);
            MoveWindow(showHiddenBox_, page.left, top, boxW, rowH, TRUE);
            top += rowH + gap / 2;
        }
        MoveWindow(t->list, page.left, top, pw, std::max(0, static_cast<int>(page.bottom) - top), TRUE);

        RECT client;
        GetClientRect(t->list, &client);
        int nameW = client.right;
        for (size_t c = 1; c < t->columns.size(); ++c) {
            const int w = Scale(t->columns[c] == TableColumn::Type ? kTypeColumn : kNumberColumn);
            ListView_SetColumnWidth(t->list, static_cast<int>(c), w);
            nameW -= w;
        }
        ListView_SetColumnWidth(t->list, 0, std::max(Scale(120), nameW));
        ListView_SetColumnWidth(t->list, static_cast<int>(t->columns.size()) - 1, LVSCW_AUTOSIZE_USEHEADER);
    }
}

void ReportWindow::SelectTab(int tab) {
    tab_ = tab;
    if (!tabs_) return;
    TabCtrl_SetCurSel(tabs_, tab);
    auto show = [](HWND w, bool on) { ShowWindow(w, on ? SW_SHOWNA : SW_HIDE); };
    show(summary_, tab == 0);
    show(timeline_, tab == 1);
    show(adaptersNote_, tab == 2);
    show(showHiddenBox_, tab == 2);
    show(adapters_->list, tab == 2);
    show(wifiNote_, tab == 3);
    show(wifi_->list, tab == 3);
    SetHot(-1, false);
    InvalidateRect(tabs_, nullptr, FALSE);
    HWND focus = GetFocus();
    if (focus && IsChild(hwnd_, focus) && !IsWindowVisible(focus)) SetFocus(tabs_);
}

bool ReportWindow::PreTranslateMessage(MSG& msg) {
    if (!hwnd_ || (msg.hwnd != hwnd_ && !IsChild(hwnd_, msg.hwnd))) return false;
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && (GetKeyState(VK_CONTROL) & 0x8000)) {
        const bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        SelectTab((tab_ + (back ? kTabCount - 1 : 1)) % kTabCount);
        return true;
    }
    return IsDialogMessageW(hwnd_, &msg) != FALSE;
}

void ReportWindow::ReportBounds() {
    // Not IsWindowVisible: DestroyWindow hides the window before WM_DESTROY, where this runs last.
    if (!hwnd_ || IsIconic(hwnd_) || IsZoomed(hwnd_)) return;
    RECT r;
    if (!GetWindowRect(hwnd_, &r)) return;
    if (savedRect_ && EqualRect(&*savedRect_, &r)) return;
    savedRect_ = r;
    if (onBoundsChanged) onBoundsChanged(r);
}

UtcRange ReportWindow::CurrentRange(int64_t nowUtc, const TimeZone& tz, int firstDayOfWeek) const {
    return ResolvePeriod(kPeriods[periodIndex_].period, nowUtc, tz, firstDayOfWeek, customFrom_, customTo_);
}

void ReportWindow::Refresh() {
    if (!hwnd_) return;
    if (beforeQuery) beforeQuery();  // write pending usage so the report includes it
    if (!hwnd_) return;
    const TimeZone tz = TimeZone::Local();  // a snapshot: fetched fresh each time
    const int64_t now = NowUtc();
    const int firstDay = FirstDayOfWeekFromLocale();
    const UtcRange range = CurrentRange(now, tz, firstDay);
    ReportModel model(store_, tz, firstDay);

    const std::wstring rangeText = range.begin == 0 || range.end <= range.begin
                                       ? std::wstring{}
                                       : RangeLabel(LocalDateOf(range.begin, tz), LocalDateOf(range.end - 1, tz));
    SetWindowTextW(range_, rangeText.c_str());

    // Summary.
    const SummaryData s = model.Summary(range, now);
    auto detail = [](const Totals& t) {
        return FillPlaceholders(LoadStr(IDS_RX_TX), {FormatVolume(t.rx), FormatVolume(t.tx)});
    };
    auto card = [&](std::wstring title, const Totals& t, bool highlight) {
        return SummaryCard{std::move(title), FormatVolume(t.Total()), detail(t), highlight};
    };
    const std::wstring none = LoadStr(IDS_NO_USAGE), unknownWifi = LoadStr(IDS_UNKNOWN_WIFI);
    SummaryView view;
    if (!corruptFile_.empty()) {
        view.notices.push_back(FillPlaceholders(LoadStr(IDS_NOTE_CORRUPT), {corruptFile_.wstring()}));
    }
    if (!store_.Healthy()) view.notices.push_back(LoadStr(IDS_NOTE_UNHEALTHY));
    const std::wstring periodName = periodIndex_ == kCustomPeriod ? rangeText : LoadStr(kPeriods[periodIndex_].name);
    const int fixed = FixedSummaryCard(kPeriods[periodIndex_].period);
    view.cards = {card(LoadStr(IDS_PERIOD_TODAY), s.today, fixed == 0),
                  card(LoadStr(IDS_PERIOD_THIS_WEEK), s.thisWeek, fixed == 1),
                  card(LoadStr(IDS_PERIOD_THIS_MONTH), s.thisMonth, fixed == 2)};
    if (fixed < 0) view.cards.push_back(card(periodName, s.period, true));
    SummaryPanel busiest{LoadStr(IDS_BUSIEST_DAY), {}, none};
    if (s.busiestDay && s.busiestDay->second.Total() > 0) {
        busiest.rows.push_back({LongDate(s.busiestDay->first), FormatVolume(s.busiestDay->second.Total())});
        busiest.rows.push_back({detail(s.busiestDay->second), L""});
    }
    SummaryPanel topAdapters{LoadStr(IDS_TOP_ADAPTERS), {}, none};
    for (const auto& a : s.topAdapters) topAdapters.rows.push_back({a.name, FormatVolume(a.totals.Total())});
    SummaryPanel topNetworks{LoadStr(IDS_TOP_WIFI), {}, none};
    for (const auto& n : s.topNetworks) {
        topNetworks.rows.push_back({n.name.empty() ? unknownWifi : n.name, FormatVolume(n.totals.Total())});
    }
    view.panels = {std::move(busiest), std::move(topAdapters), std::move(topNetworks)};
    view.coverage = LoadStr(IDS_NOTE_COVERAGE);
    summaryView_ = std::move(view);

    // Timeline.
    SetHot(-1, false);
    timelineView_.granularity = model.PickGranularity(range, now);
    timelineView_.bars = model.Timeline(range, now);
    timelineView_.axisLabels = AxisLabels(timelineView_.bars, timelineView_.granularity);
    timelineView_.receivedLegend = LoadStr(IDS_LEGEND_RECEIVED);
    timelineView_.sentLegend = LoadStr(IDS_LEGEND_SENT);
    timelineView_.emptyText = none;
    timelineLayout_ = {};

    // Tables: raw per-adapter traffic.
    auto toRow = [&](const NamedTotals& n, bool wifi) {
        TableRow row;
        row.data = n;
        if (wifi) row.name = n.name.empty() ? unknownWifi : n.name;
        else row.name = n.hidden ? FillPlaceholders(LoadStr(IDS_ADAPTER_HIDDEN), {n.name}) : n.name;
        row.type = KindName(n.kind);
        return row;
    };
    const std::wstring adapterSel = SelectedId(*adapters_), wifiSel = SelectedId(*wifi_);
    adapters_->rows.clear();
    for (const auto& n : model.AdapterTable(range, showHiddenRows_)) adapters_->rows.push_back(toRow(n, false));
    wifi_->rows.clear();
    for (const auto& n : model.WifiTable(range)) wifi_->rows.push_back(toRow(n, true));
    FillTable(*adapters_, adapterSel);
    FillTable(*wifi_, wifiSel);

    InvalidateRect(summary_, nullptr, FALSE);
    InvalidateRect(timeline_, nullptr, FALSE);
}

std::wstring ReportWindow::SelectedId(const TableState& t) const {
    if (!t.list) return {};
    const int sel = ListView_GetNextItem(t.list, -1, LVNI_SELECTED);
    return sel >= 0 && static_cast<size_t>(sel) < t.rows.size() ? t.rows[static_cast<size_t>(sel)].data.id
                                                                  : std::wstring{};
}

void ReportWindow::FillTable(TableState& t, const std::wstring& selectedId) {
    if (!t.list) return;
    SortRows(t.rows, t.sort);
    SendMessageW(t.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(t.list);
    for (size_t i = 0; i < t.rows.size(); ++i) {
        const TableRow& row = t.rows[i];
        for (size_t c = 0; c < t.columns.size(); ++c) {
            std::wstring text;
            switch (t.columns[c]) {
                case TableColumn::Name: text = row.name; break;
                case TableColumn::Type: text = row.type; break;
                case TableColumn::Received: text = FormatVolume(row.data.totals.rx); break;
                case TableColumn::Sent: text = FormatVolume(row.data.totals.tx); break;
                case TableColumn::Total: text = FormatVolume(row.data.totals.Total()); break;
            }
            if (c == 0) {
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = static_cast<int>(i);
                item.pszText = text.data();
                ListView_InsertItem(t.list, &item);
            } else {
                ListView_SetItemText(t.list, static_cast<int>(i), static_cast<int>(c), text.data());
            }
        }
        if (!selectedId.empty() && row.data.id == selectedId) {
            ListView_SetItemState(t.list, static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
    HWND header = ListView_GetHeader(t.list);
    for (size_t c = 0; c < t.columns.size(); ++c) {
        HDITEMW hd{};
        hd.mask = HDI_FORMAT;
        Header_GetItem(header, static_cast<int>(c), &hd);
        hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (t.columns[c] == t.sort.column) hd.fmt |= t.sort.ascending ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(header, static_cast<int>(c), &hd);
    }
    SendMessageW(t.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(t.list, nullptr, TRUE);
}

void ReportWindow::OnPeriodPicked() {
    if (!hwnd_) return;
    const int sel = ComboBox_GetCurSel(period_);
    if (sel < 0 || sel >= static_cast<int>(std::size(kPeriods))) return;
    if (sel == kCustomPeriod) {
        const TimeZone tz = TimeZone::Local();
        const int64_t now = NowUtc();
        LocalDate from = customFrom_, to = customTo_;
        if (periodIndex_ != kCustomPeriod) {  // start from the period shown now
            const UtcRange r = CurrentRange(now, tz, FirstDayOfWeekFromLocale());
            from = LocalDateOf(r.begin == 0 ? now : r.begin, tz);
            to = LocalDateOf(r.end - 1, tz);
        }
        const auto picked = CustomRangeDialog(hwnd_, from, to, theme_ == ReportTheme::Dark);
        if (!hwnd_) return;
        if (!picked) {
            ComboBox_SetCurSel(period_, periodIndex_);
            return;
        }
        customFrom_ = picked->first;
        customTo_ = picked->second;
    } else if (sel == periodIndex_) {
        return;
    }
    periodIndex_ = sel;
    Refresh();
}

void ReportWindow::AdaptersChanged() {
    if (onAdaptersChanged) onAdaptersChanged();  // the overlay menu and tooltip use the new name at once
    Refresh();
}

void ReportWindow::ShowRowMenu(HWND list, int item, POINT screenPt) {
    TableState& t = list == adapters_->list ? *adapters_ : *wifi_;
    if (item < 0 || static_cast<size_t>(item) >= t.rows.size()) return;
    const NamedTotals row = t.rows[static_cast<size_t>(item)].data;  // a copy: Refresh replaces the rows
    const std::wstring name = t.adapters ? row.name : row.name.empty() ? LoadStr(IDS_UNKNOWN_WIFI) : row.name;

    HMENU menu = CreatePopupMenu();
    if (t.adapters) {
        AppendMenuW(menu, MF_STRING, kCmdRename, LoadStr(IDS_MENU_RENAME).c_str());
        AppendMenuW(menu, MF_STRING, kCmdHide, LoadStr(row.hidden ? IDS_MENU_UNHIDE : IDS_MENU_HIDE).c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCmdClearAdapter, LoadStr(IDS_MENU_CLEAR_ADAPTER).c_str());
    } else {
        // Rows without a network key are traffic Windows couldn't name. Clearing by an empty key would
        // also delete every other adapter's rows that have no network, so it is not offered.
        AppendMenuW(menu, MF_STRING | (row.id.empty() ? MF_GRAYED : 0u), kCmdClearNetwork,
                    LoadStr(IDS_MENU_CLEAR_NETWORK).c_str());
    }
    const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    const UINT cmd = static_cast<UINT>(
        TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | align, screenPt.x, screenPt.y, hwnd_, nullptr));
    DestroyMenu(menu);
    if (!hwnd_) return;

    switch (cmd) {
        case kCmdRename: {
            std::wstring windowsName = row.id;
            for (const auto& a : store_.Adapters()) {
                if (a.id == row.id && !a.name.empty()) windowsName = a.name;
            }
            const auto renamed = RenameDialog(hwnd_, row.name, windowsName, theme_ == ReportTheme::Dark);
            if (!renamed || !hwnd_) return;
            store_.SetFriendlyName(row.id, *renamed);  // empty restores the Windows name
            AdaptersChanged();
            return;
        }
        case kCmdHide:
            store_.SetHidden(row.id, !row.hidden);
            AdaptersChanged();
            return;
        case kCmdClearAdapter:
        case kCmdClearNetwork:
            if (!ConfirmDelete(hwnd_, FillPlaceholders(LoadStr(IDS_CONFIRM_CLEAR_ONE), {name})) || !hwnd_) return;
            if (beforeQuery) beforeQuery();  // pending rows go first, so they are cleared too
            if (cmd == kCmdClearAdapter) store_.ClearAdapter(row.id);
            else store_.ClearNetwork(row.id);
            Refresh();
            return;
        default:
            return;
    }
}

void ReportWindow::ConfirmClearAll() {
    if (!ConfirmDelete(hwnd_, LoadStr(IDS_CONFIRM_CLEAR_ALL)) || !hwnd_) return;
    if (beforeQuery) beforeQuery();
    store_.ClearAll();
    Refresh();
}

void ReportWindow::ExportCsvFile() {
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    const std::wstring filter = LoadStr(IDS_CSV_FILTER), title = LoadStr(IDS_EXPORT_TITLE);
    const COMDLG_FILTERSPEC spec{filter.c_str(), L"*.csv"};
    dialog->SetFileTypes(1, &spec);
    dialog->SetDefaultExtension(L"csv");
    dialog->SetTitle(title.c_str());
    dialog->SetFileName(DefaultCsvName(LocalDateOf(NowUtc(), TimeZone::Local())).c_str());
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOREADONLYRETURN);
    if (FAILED(dialog->Show(hwnd_)) || !hwnd_) return;

    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return;
    const std::filesystem::path file(path);
    CoTaskMemFree(path);

    if (beforeQuery) beforeQuery();
    const TimeZone tz = TimeZone::Local();
    // The selected period, every adapter and network: the same rows all four tabs are built from.
    const CsvExportResult result = ExportCsv(file, store_, CurrentRange(NowUtc(), tz, FirstDayOfWeekFromLocale()), tz);
    if (result != CsvExportResult::Ok && hwnd_) {
        ShowError(hwnd_, LoadStr(result == CsvExportResult::ReadFailed ? IDS_EXPORT_READ_FAILED : IDS_EXPORT_FAILED));
    }
}

// ---- Painting ----

void ReportWindow::PaintCanvas(HWND canvas, HDC dc) {
    RECT rc;
    GetClientRect(canvas, &rc);
    if (rc.right <= 0 || rc.bottom <= 0 || !painter_ || !d2d_) return;
    if (!target_) {
        // Software rendering into the paint DC: no GPU driver is loaded for a few rectangles, so closing the
        // window frees it all, and WM_PRINTCLIENT works the same way as WM_PAINT.
        const auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (FAILED(d2d_->CreateDCRenderTarget(&props, &target_))) return;
    }
    if (FAILED(target_->BindDC(dc, &rc))) return;
    target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    const D2D1_SIZE_F size = target_->GetSize();
    target_->BeginDraw();
    if (canvas == summary_) {
        painter_->PaintSummary(target_.Get(), size, summaryView_, palette_);
    } else {
        timelineLayout_ = painter_->PaintTimeline(target_.Get(), size, timelineView_, palette_);
    }
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) target_.Reset();
}

void ReportWindow::PaintTabs(HWND tabs) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(tabs, &ps);
    RECT rc;
    GetClientRect(tabs, &rc);
    FillRect(dc, &rc, backgroundBrush_);
    HBRUSH border = CreateSolidBrush(palette_.border);
    HBRUSH surface = CreateSolidBrush(palette_.surface);
    HBRUSH accent = CreateSolidBrush(palette_.accent);
    const RECT baseline{rc.left, rc.bottom - 1, rc.right, rc.bottom};
    FillRect(dc, &baseline, border);
    HGDIOBJ oldFont = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    const int selected = TabCtrl_GetCurSel(tabs);
    const LRESULT ui = SendMessageW(tabs, WM_QUERYUISTATE, 0, 0);
    for (int i = 0; i < TabCtrl_GetItemCount(tabs); ++i) {
        RECT r{};
        TabCtrl_GetItemRect(tabs, i, &r);
        r.bottom = rc.bottom - 1;
        if (i == selected) {
            FillRect(dc, &r, surface);
            const RECT bar{r.left, r.bottom - Scale(2), r.right, r.bottom};
            FillRect(dc, &bar, accent);
        }
        wchar_t text[64] = {};
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = text;
        item.cchTextMax = static_cast<int>(std::size(text));
        TabCtrl_GetItem(tabs, i, &item);
        SetTextColor(dc, i == selected ? palette_.text : palette_.muted);
        DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (i == selected && GetFocus() == tabs && !(ui & UISF_HIDEFOCUS)) {
            RECT focus = r;
            InflateRect(&focus, -Scale(3), -Scale(3));
            SetTextColor(dc, palette_.text);
            DrawFocusRect(dc, &focus);
        }
    }
    SelectObject(dc, oldFont);
    DeleteObject(border);
    DeleteObject(surface);
    DeleteObject(accent);
    EndPaint(tabs, &ps);
}

LRESULT ReportWindow::DrawDarkCheckbox(NMCUSTOMDRAW* cd) {
    if (cd->dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    HDC dc = cd->hdc;
    const RECT rc = cd->rc;
    FillRect(dc, &rc, backgroundBrush_);
    const bool checked = Button_GetCheck(showHiddenBox_) == BST_CHECKED;
    int state = checked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL;
    if (cd->uItemState & CDIS_SELECTED) state += 2;  // pressed
    else if (cd->uItemState & CDIS_HOT) state += 1;  // hot
    HTHEME theme = OpenThemeDataForDpi(showHiddenBox_, L"Button", dpi_);
    SIZE box{Scale(13), Scale(13)};
    if (theme) GetThemePartSize(theme, dc, BP_CHECKBOX, state, nullptr, TS_DRAW, &box);
    RECT boxRect{rc.left, rc.top + (rc.bottom - rc.top - box.cy) / 2, rc.left + box.cx, 0};
    boxRect.bottom = boxRect.top + box.cy;
    if (theme) {
        DrawThemeBackground(theme, dc, BP_CHECKBOX, state, &boxRect, nullptr);
        CloseThemeData(theme);
    } else {
        DrawFrameControl(dc, &boxRect, DFC_BUTTON, DFCS_BUTTONCHECK | (checked ? DFCS_CHECKED : 0u));
    }
    const LRESULT ui = SendMessageW(showHiddenBox_, WM_QUERYUISTATE, 0, 0);
    const UINT format = DT_SINGLELINE | DT_VCENTER | DT_LEFT | ((ui & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0u);
    const std::wstring label = LoadStr(IDS_SHOW_HIDDEN);
    RECT text = rc;
    text.left = boxRect.right + Scale(5);
    HGDIOBJ old = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, palette_.text);
    DrawTextW(dc, label.c_str(), -1, &text, format);
    if ((cd->uItemState & CDIS_FOCUS) && !(ui & UISF_HIDEFOCUS)) {
        RECT focus = text;
        DrawTextW(dc, label.c_str(), -1, &focus, format | DT_CALCRECT);
        const int h = focus.bottom - focus.top;
        focus.top = text.top + (text.bottom - text.top - h) / 2;
        focus.bottom = focus.top + h;
        InflateRect(&focus, 2, 1);
        DrawFocusRect(dc, &focus);
    }
    SelectObject(dc, old);
    return CDRF_SKIPDEFAULT;
}

std::wstring ReportWindow::BarTip(int index) const {
    const TimelineBar& bar = timelineView_.bars[static_cast<size_t>(index)];
    return BarTitle(bar.start, timelineView_.granularity) + L"\r\n" +
           FillPlaceholders(LoadStr(IDS_TIP_RECEIVED), {FormatVolume(bar.totals.rx)}) + L"\r\n" +
           FillPlaceholders(LoadStr(IDS_TIP_SENT), {FormatVolume(bar.totals.tx)}) + L"\r\n" +
           FillPlaceholders(LoadStr(IDS_TIP_TOTAL), {FormatVolume(bar.totals.Total())});
}

void ReportWindow::SetHot(int index, bool fromKeyboard) {
    if (index < 0 || static_cast<size_t>(index) >= timelineLayout_.bars.size() ||
        static_cast<size_t>(index) >= timelineView_.bars.size())
        index = -1;
    const bool changed = index != timelineView_.hot;
    timelineView_.hot = index;
    if (changed && timeline_) InvalidateRect(timeline_, nullptr, FALSE);
    if (!tooltip_) return;
    TTTOOLINFOW ti{sizeof(ti)};
    ti.hwnd = timeline_;
    ti.uId = 1;
    if (index < 0) {
        SendMessageW(tooltip_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&ti));
        return;
    }
    if (changed) {
        tipText_ = BarTip(index);
        ti.lpszText = tipText_.data();
        SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
    POINT pt{};
    if (fromKeyboard) {
        const BarGeometry& g = timelineLayout_.bars[static_cast<size_t>(index)];
        const float scale = static_cast<float>(dpi_) / 96.0f;
        pt = POINT{static_cast<LONG>(g.slotRight * scale) + Scale(4),
                   static_cast<LONG>(timelineLayout_.plot.top * scale)};
        ClientToScreen(timeline_, &pt);
    } else {
        GetCursorPos(&pt);
        pt.x += Scale(16);
        pt.y += Scale(20);
    }
    SendMessageW(tooltip_, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x, pt.y));
    SendMessageW(tooltip_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&ti));
}

// ---- Window procedures ----

LRESULT CALLBACK ReportWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<ReportWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<ReportWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->OnDestroy();
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self->HandleMessage(msg, wp, lp);
}

LRESULT CALLBACK ReportWindow::CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HWND parent = GetParent(hwnd);
    auto* self = parent ? reinterpret_cast<ReportWindow*>(GetWindowLongPtrW(parent, GWLP_USERDATA)) : nullptr;
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->HandleCanvas(hwnd, msg, wp, lp);
}

LRESULT CALLBACK ReportWindow::TabsSubclass(HWND tabs, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    auto* self = reinterpret_cast<ReportWindow*>(ref);
    // The tab control has no dark theme, and its light one paints a grey band behind a strip-only control:
    // draw it here, except in High Contrast, where the system look is the right one.
    if (self->theme_ != ReportTheme::HighContrast) {
        if (msg == WM_ERASEBKGND) return 1;
        if (msg == WM_PAINT) {
            self->PaintTabs(tabs);
            return 0;
        }
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(tabs, &ReportWindow::TabsSubclass, id);
    return DefSubclassProc(tabs, msg, wp, lp);
}

LRESULT CALLBACK ReportWindow::ListSubclass(HWND list, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    auto* self = reinterpret_cast<ReportWindow*>(ref);
    if (msg == WM_NOTIFY && self->theme_ == ReportTheme::Dark) {
        // The dark header theme keeps black text: colour it here.
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (nm->code == NM_CUSTOMDRAW && nm->hwndFrom == ListView_GetHeader(list)) {
            auto* cd = reinterpret_cast<NMCUSTOMDRAW*>(lp);
            if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(cd->hdc, self->palette_.text);
                return CDRF_DODEFAULT;
            }
        }
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(list, &ReportWindow::ListSubclass, id);
    return DefSubclassProc(list, msg, wp, lp);
}

LRESULT ReportWindow::HandleCanvas(HWND canvas, UINT msg, WPARAM wp, LPARAM lp) {
    const bool timeline = canvas == timeline_;
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(canvas, &ps);
            PaintCanvas(canvas, dc);
            EndPaint(canvas, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            PaintCanvas(canvas, reinterpret_cast<HDC>(wp));
            return 0;
        case WM_SIZE:
            InvalidateRect(canvas, nullptr, FALSE);
            return 0;
        case WM_GETDLGCODE:
            return timeline ? DLGC_WANTARROWS : 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            if (timeline) {
                timelineView_.focused = msg == WM_SETFOCUS;
                if (msg == WM_KILLFOCUS) SetHot(-1, false);
                InvalidateRect(canvas, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (timeline) SetFocus(canvas);
            return 0;
        case WM_MOUSEMOVE:
            if (timeline) {
                if (!trackingMouse_) {
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, canvas, 0};
                    trackingMouse_ = TrackMouseEvent(&tme) != FALSE;
                }
                const float toDip = 96.0f / static_cast<float>(dpi_);
                SetHot(HitTestBar(timelineLayout_.bars, timelineLayout_.plot,
                                  static_cast<float>(GET_X_LPARAM(lp)) * toDip,
                                  static_cast<float>(GET_Y_LPARAM(lp)) * toDip),
                       false);
            }
            return 0;
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            SetHot(-1, false);
            return 0;
        case WM_KEYDOWN:
            if (timeline && !timelineLayout_.bars.empty()) {
                const int last = static_cast<int>(timelineLayout_.bars.size()) - 1;
                const int hot = timelineView_.hot;
                int next = hot;
                switch (wp) {
                    case VK_LEFT: next = hot < 0 ? last : std::max(0, hot - 1); break;
                    case VK_RIGHT: next = hot < 0 ? 0 : std::min(last, hot + 1); break;
                    case VK_HOME: next = 0; break;
                    case VK_END: next = last; break;
                    default: return DefWindowProcW(canvas, msg, wp, lp);
                }
                SetHot(next, true);
                return 0;
            }
            break;
    }
    return DefWindowProcW(canvas, msg, wp, lp);
}

LRESULT ReportWindow::HandleNotify(NMHDR* nm) {
    if (nm->hwndFrom == tabs_ && nm->code == TCN_SELCHANGE) {
        SelectTab(TabCtrl_GetCurSel(tabs_));
        return 0;
    }
    if (nm->hwndFrom == showHiddenBox_ && nm->code == NM_CUSTOMDRAW && theme_ == ReportTheme::Dark) {
        return DrawDarkCheckbox(reinterpret_cast<NMCUSTOMDRAW*>(nm));
    }
    for (TableState* t : {adapters_.get(), wifi_.get()}) {
        if (nm->hwndFrom != t->list || nm->code != LVN_COLUMNCLICK) continue;
        const int column = reinterpret_cast<NMLISTVIEW*>(nm)->iSubItem;
        if (column >= 0 && static_cast<size_t>(column) < t->columns.size()) {
            const std::wstring selected = SelectedId(*t);
            t->sort = NextSort(t->sort, t->columns[static_cast<size_t>(column)]);
            FillTable(*t, selected);
        }
        return 0;
    }
    return 0;
}

LRESULT ReportWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            dpi_ = GetDpiForWindow(hwnd_);
            CreateControls();
            ApplyFont();
            ApplyTheme();
            Layout();
            SelectTab(tab_);
            Refresh();
            SetTimer(hwnd_, kRefreshTimer, kRefreshMs, nullptr);
            return 0;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize = POINT{Scale(kMinSize.cx), Scale(kMinSize.cy)};
            if (painter_ && pageTop_ > 0 && pageWidth_ > 0) {
                // Tall enough for the summary, banners included, at the current width.
                const float widthDip = static_cast<float>(pageWidth_) * 96.0f / static_cast<float>(dpi_);
                const int summaryPx = Scale(static_cast<int>(painter_->SummaryMinHeight(summaryView_, widthDip)));
                RECT r{0, 0, 0, pageTop_ + summaryPx + Scale(kMargin)};
                AdjustWindowRectExForDpi(&r, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), FALSE, 0, dpi_);
                mmi->ptMinTrackSize.y = std::max<LONG>(mmi->ptMinTrackSize.y, r.bottom - r.top);
            }
            return 0;
        }
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            const RECT* suggested = reinterpret_cast<const RECT*>(lp);
            ApplyFont();
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, backgroundBrush_);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            const HWND ctl = reinterpret_cast<HWND>(lp);
            const bool note = ctl == adaptersNote_ || ctl == wifiNote_ || ctl == range_;
            SetTextColor(dc, note ? palette_.muted : palette_.text);
            SetBkColor(dc, palette_.background);
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        }
        case WM_CTLCOLORLISTBOX:  // the period drop-down
            if (theme_ != ReportTheme::Dark) break;
            SetTextColor(reinterpret_cast<HDC>(wp), palette_.text);
            SetBkColor(reinterpret_cast<HDC>(wp), palette_.background);
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case kIdPeriod:
                    // A pick from the open list (CBN_SELENDOK, even of the same item) or an arrow key on the
                    // closed box. Handled after the combo box finishes, since it may open a dialog.
                    if (HIWORD(wp) == CBN_SELENDOK ||
                        (HIWORD(wp) == CBN_SELCHANGE && !ComboBox_GetDroppedState(period_))) {
                        if (!periodPending_) {
                            periodPending_ = true;
                            PostMessageW(hwnd_, WM_APP_PERIOD, 0, 0);
                        }
                    }
                    return 0;
                case kIdExport:
                    if (HIWORD(wp) == BN_CLICKED) ExportCsvFile();
                    return 0;
                case kIdClearAll:
                    if (HIWORD(wp) == BN_CLICKED) ConfirmClearAll();
                    return 0;
                case kIdShowHidden:
                    if (HIWORD(wp) == BN_CLICKED) {
                        showHiddenRows_ = Button_GetCheck(showHiddenBox_) == BST_CHECKED;
                        Refresh();
                    }
                    return 0;
                case IDCANCEL:  // Esc, via IsDialogMessage
                    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                    return 0;
            }
            break;
        case WM_APP_PERIOD:
            periodPending_ = false;
            OnPeriodPicked();
            return 0;
        case WM_NOTIFY:
            return HandleNotify(reinterpret_cast<NMHDR*>(lp));
        case WM_CONTEXTMENU: {
            const HWND src = reinterpret_cast<HWND>(wp);
            TableState* t = src == adapters_->list ? adapters_.get() : src == wifi_->list ? wifi_.get() : nullptr;
            if (!t) break;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int item = -1;
            if (lp == -1) {  // keyboard: Shift+F10 or the menu key
                item = ListView_GetNextItem(src, -1, LVNI_SELECTED);
                if (item < 0) return 0;
                RECT r{};
                ListView_GetItemRect(src, item, &r, LVIR_LABEL);
                pt = POINT{r.left, r.bottom};
                ClientToScreen(src, &pt);
            } else {
                LVHITTESTINFO hit{};
                hit.pt = pt;
                ScreenToClient(src, &hit.pt);
                item = ListView_HitTest(src, &hit);
                if (item < 0) return 0;
                ListView_SetItemState(src, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            ShowRowMenu(src, item, pt);
            return 0;
        }
        case WM_TIMER:
            if (wp == kRefreshTimer && IsWindowVisible(hwnd_) && !IsIconic(hwnd_)) Refresh();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) {
                HWND focus = GetFocus();
                lastFocus_ = focus && IsChild(hwnd_, focus) ? focus : nullptr;
                SetHot(-1, false);
            } else if (!HIWORD(wp)) {  // not minimized
                SetFocus(lastFocus_ && IsWindowVisible(lastFocus_) ? lastFocus_ : tabs_);
            }
            return 0;
        case WM_SETTINGCHANGE: {
            const auto* area = reinterpret_cast<const wchar_t*>(lp);
            if (wp == SPI_SETHIGHCONTRAST || (area && wcscmp(area, L"ImmersiveColorSet") == 0)) {
                ApplyTheme();
            } else if (wp == SPI_SETNONCLIENTMETRICS) {
                ApplyFont();
                Layout();
            } else if (area && wcscmp(area, L"intl") == 0) {
                Refresh();  // date formats and the first day of the week
            }
            break;
        }
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            ApplyTheme();
            break;
        case WM_EXITSIZEMOVE:
            ReportBounds();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            ReportBounds();
            KillTimer(hwnd_, kRefreshTimer);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void ReportWindow::OnDestroy() {
    // Everything the window needed goes with it; only the period choice, sort order and saved rect stay.
    hwnd_ = nullptr;
    periodLabel_ = period_ = range_ = export_ = clearAll_ = tabs_ = summary_ = timeline_ = tooltip_ = nullptr;
    adaptersNote_ = showHiddenBox_ = wifiNote_ = lastFocus_ = nullptr;
    adapters_->list = wifi_->list = nullptr;
    adapters_->rows = {};
    wifi_->rows = {};
    target_.Reset();
    painter_.reset();
    d2d_.Reset();
    if (font_) DeleteObject(font_);
    font_ = nullptr;
    if (backgroundBrush_) DeleteObject(backgroundBrush_);
    backgroundBrush_ = nullptr;
    summaryView_ = {};
    timelineView_ = {};
    timelineLayout_ = {};
    tipText_ = {};
    trackingMouse_ = false;
    periodPending_ = false;
    pageTop_ = pageWidth_ = 0;
}

}  // namespace nsm
