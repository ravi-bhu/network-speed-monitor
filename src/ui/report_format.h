#pragma once
#include <string>
#include <vector>

#include "core/types.h"
#include "report/report_model.h"

namespace nsm {

// The report window's table sorting and locale formatting (exposed for tests).

enum class TableColumn { Name, Type, Received, Sent, Total };
struct TableSort {
    TableColumn column = TableColumn::Total;
    bool ascending = false;
};
// Clicking the sorted column flips its direction; another column starts A–Z for text, largest first for numbers.
TableSort NextSort(TableSort current, TableColumn clicked);
bool IsNumeric(TableColumn c);  // Received, Sent and Total

struct TableRow {
    NamedTotals data;
    std::wstring name;  // as displayed
    std::wstring type;  // as displayed
};
// Ties fall back to the larger total, then the name, then the id.
void SortRows(std::vector<TableRow>& rows, TableSort sort);

std::wstring DefaultCsvName(LocalDate today);  // network-usage-YYYY-MM-DD.csv

// Locale formatting. `locale` nullptr = the user's locale with their overrides; a locale name uses that
// locale's defaults (tests).
std::wstring RangeLabel(LocalDate first, LocalDate last, const wchar_t* locale = nullptr);
// One x-axis label per bar: hours (with minutes when bars start off the hour, as in UTC+5:30), days (weekday
// for a week or less; the month on the first bar and on the 1st), or months (with the year on the first bar
// and in January when the bars span more than one year).
std::vector<std::wstring> AxisLabels(const std::vector<TimelineBar>& bars, Granularity g,
                                     const wchar_t* locale = nullptr);
// The tooltip's first line: the hour (from its start to its end, after `lengthSec`), day or month a bar covers.
std::wstring BarTitle(const TimelineBar& bar, Granularity g, const wchar_t* locale = nullptr);
std::wstring LongDate(LocalDate d, const wchar_t* locale = nullptr);

}  // namespace nsm
