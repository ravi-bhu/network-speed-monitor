#pragma once
#include <filesystem>
#include <string>
#include <string_view>

#include "core/time_zone.h"
#include "core/types.h"
#include "storage/usage_store.h"

namespace nsm {

// RFC 4180 field quoting: wraps in quotes (doubling inner quotes) when the text has a comma, quote, CR or LF.
std::string CsvEscape(std::string_view utf8);

// A text field (names, ids, dates; never the byte counts) as written to the file. Text that a spreadsheet
// would run as a formula (first character =, +, -, @, TAB or CR) gets a leading ' first, then CsvEscape.
// Network names (SSIDs) and adapter names come from outside, so an export must not carry live formulas.
std::string CsvTextField(std::string_view utf8);

// Writes every stored row in the range (hourly rows, then daily rows) as UTF-8 with BOM and CRLF endings.
// Returns false on any I/O failure and leaves no partial file behind.
bool ExportCsv(const std::filesystem::path& file, UsageStore& store, UtcRange r, const TimeZone& tz);

}  // namespace nsm
