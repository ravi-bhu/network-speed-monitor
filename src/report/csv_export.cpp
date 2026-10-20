#include "report/csv_export.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "core/civil_date.h"
#include "core/periods.h"
#include "core/utf8.h"

namespace nsm {
namespace {

const char* KindName(AdapterKind k) {
    switch (k) {
    case AdapterKind::WiFi: return "wifi";
    case AdapterKind::Ethernet: return "ethernet";
    case AdapterKind::Virtual: return "virtual";
    case AdapterKind::Other: break;
    }
    return "other";
}

struct Lookup {
    std::unordered_map<std::wstring, AdapterMeta> adapters;
    std::unordered_map<std::wstring, std::wstring> networks;
};

void AppendRow(std::string& out, const std::string& start, const char* granularity, Series series,
               const std::wstring& adapterId, const std::wstring& networkKey, uint64_t rx, uint64_t tx,
               const std::string& startUtc, const Lookup& lk) {
    std::wstring name = adapterId;
    AdapterKind kind = AdapterKind::Other;
    if (auto it = lk.adapters.find(adapterId); it != lk.adapters.end()) {
        const AdapterMeta& m = it->second;
        name = !m.friendlyName.empty() ? m.friendlyName : (!m.name.empty() ? m.name : m.id);
        kind = m.kind;
    }
    std::string network;
    if (!networkKey.empty()) {
        if (auto it = lk.networks.find(networkKey); it != lk.networks.end()) network = ToUtf8(it->second);
    }
    out += CsvTextField(start);                      out += ',';
    out += granularity;                              out += ',';
    out += series == Series::Internet ? "internet" : "adapter"; out += ',';
    out += CsvTextField(ToUtf8(adapterId));          out += ',';
    out += CsvTextField(ToUtf8(name));               out += ',';
    out += KindName(kind);                           out += ',';
    out += CsvTextField(network);                    out += ',';
    out += std::to_string(rx);                       out += ',';
    out += std::to_string(tx);                       out += ',';
    out += CsvTextField(startUtc);                   out += "\r\n";
}

}  // namespace

std::string CsvEscape(std::string_view s) {
    if (s.find_first_of(",\"\r\n") == std::string_view::npos) return std::string(s);
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

std::string CsvTextField(std::string_view s) {
    // OWASP "CSV injection": a leading ' makes Excel and LibreOffice show the text instead of evaluating it.
    if (!s.empty() && std::string_view("=+-@\t\r").find(s.front()) != std::string_view::npos) {
        std::string quoted = "'";
        quoted += s;
        return CsvEscape(quoted);
    }
    return CsvEscape(s);
}

std::string CsvUtcTime(int64_t utcSec) {
    const int64_t days = utcSec / 86400 - (utcSec % 86400 < 0 ? 1 : 0);
    const int64_t rem = utcSec - days * 86400;
    const LocalDate d = CivilFromDays(days);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", d.year, d.month, d.day, static_cast<int>(rem / 3600),
                  static_cast<int>(rem % 3600 / 60));
    return buf;
}

CsvExportResult ExportCsv(const std::filesystem::path& file, UsageStore& store, UtcRange r, const TimeZone& tz) {
    Lookup lk;
    for (auto& a : store.Adapters()) lk.adapters[a.id] = a;
    for (auto& n : store.Networks()) lk.networks[n.key] = n.name;
    std::vector<UsageRow> hourly;
    std::vector<DailyRow> daily;
    if (r.end > r.begin) {
        hourly = store.QueryHourly(r);
        daily = store.QueryDaily(LocalDateOf(r.begin, tz), LocalDateOf(r.end - 1, tz));
    }
    // A failed read returns empty results, and an empty period looks the same: only Healthy() tells them apart.
    if (!store.Healthy()) return CsvExportResult::ReadFailed;

    std::string body = "\xEF\xBB\xBF";
    body += "start_local,granularity,series,adapter_id,adapter_name,adapter_kind,network,received_bytes,sent_bytes,"
            "start_utc\r\n";

    char buf[32];
    for (const auto& h : hourly) {
        const LocalDateTime t = tz.ToLocal(h.hourUtc);
        // Buckets start on the local hour, except the short one after a part-hour DST change (02:30).
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", t.year, t.month, t.day, t.hour, t.minute);
        AppendRow(body, buf, "hour", h.series, h.adapterId, h.networkKey, h.rx, h.tx, CsvUtcTime(h.hourUtc), lk);
    }
    for (const auto& d : daily) {
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", d.day.year, d.day.month, d.day.day);
        AppendRow(body, buf, "day", d.series, d.adapterId, d.networkKey, d.rx, d.tx, std::string(), lk);
    }

    std::filesystem::path tmp = file;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return CsvExportResult::WriteFailed;
        f.write(body.data(), static_cast<std::streamsize>(body.size()));
        f.flush();
        if (!f) {
            f.close();
            DeleteFileW(tmp.c_str());
            return CsvExportResult::WriteFailed;
        }
    }
    if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return CsvExportResult::WriteFailed;
    }
    return CsvExportResult::Ok;
}

}  // namespace nsm
