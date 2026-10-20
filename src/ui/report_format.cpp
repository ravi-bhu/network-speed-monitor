#include "ui/report_format.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

#include "platform/strings.h"
#include "resource.h"

namespace nsm {
namespace {

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

}  // namespace

bool IsNumeric(TableColumn c) {
    return c == TableColumn::Received || c == TableColumn::Sent || c == TableColumn::Total;
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
    const std::wstring weekday = LoadStr(IDS_AXIS_WEEKDAY), monthDay = LoadStr(IDS_AXIS_MONTH_DAY),
                       day = LoadStr(IDS_AXIS_DAY), monthYear = LoadStr(IDS_AXIS_MONTH_YEAR),
                       month = LoadStr(IDS_AXIS_MONTH);
    for (size_t i = 0; i < bars.size(); ++i) {
        const LocalDateTime& t = bars[i].start;
        const SYSTEMTIME st = SystemTimeOf(t.year, t.month, t.day, t.hour, t.minute);
        switch (g) {
            case Granularity::Hour:
                out.push_back(TimeText(st, t.minute == 0 ? TIME_NOMINUTESORSECONDS : TIME_NOSECONDS, locale));
                break;
            case Granularity::Day:
                if (bars.size() <= 8) out.push_back(DateText(st, 0, weekday.c_str(), locale));
                else out.push_back(DateText(st, 0, (i == 0 || t.day == 1 ? monthDay : day).c_str(), locale));
                break;
            case Granularity::Month:
            default:
                out.push_back(
                    DateText(st, 0, (spansYears && (i == 0 || t.month == 1) ? monthYear : month).c_str(), locale));
                break;
        }
    }
    return out;
}

std::wstring BarTitle(const TimelineBar& bar, Granularity g, const wchar_t* locale) {
    const LocalDateTime& start = bar.start;
    const SYSTEMTIME st = SystemTimeOf(start.year, start.month, start.day, start.hour, start.minute);
    switch (g) {
        case Granularity::Hour: {
            // The bucket's end on the clock it started on: usually the next full hour, but a part-hour DST
            // change ends one early (Chatham's 02:00 bucket at 02:45, when clocks jump to 03:45). With no
            // length, the next full hour. Only the time of the end is shown, so its date doesn't matter.
            const int startMinute = start.hour * 60 + start.minute;
            const int endMinute =
                bar.lengthSec > 0 ? startMinute + static_cast<int>(bar.lengthSec / 60) : (start.hour + 1) * 60;
            const SYSTEMTIME end = SystemTimeOf(start.year, start.month, start.day, endMinute / 60 % 24, endMinute % 60);
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

}  // namespace nsm
