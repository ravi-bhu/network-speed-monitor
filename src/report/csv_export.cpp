#include "report/csv_export.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <unordered_map>

#include "core/periods.h"

namespace nsm {
namespace {

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

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
               const Lookup& lk) {
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
    out += std::to_string(tx);                       out += "\r\n";
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

bool ExportCsv(const std::filesystem::path& file, UsageStore& store, UtcRange r, const TimeZone& tz) {
    Lookup lk;
    for (auto& a : store.Adapters()) lk.adapters[a.id] = a;
    for (auto& n : store.Networks()) lk.networks[n.key] = n.name;

    std::string body = "\xEF\xBB\xBF";
    body += "start_local,granularity,series,adapter_id,adapter_name,adapter_kind,network,received_bytes,sent_bytes\r\n";

    if (r.end > r.begin) {
        char buf[32];
        for (const auto& h : store.QueryHourly(r)) {
            const LocalDateTime t = tz.ToLocal(h.hourUtc);
            std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:00", t.year, t.month, t.day, t.hour);
            AppendRow(body, buf, "hour", h.series, h.adapterId, h.networkKey, h.rx, h.tx, lk);
        }
        for (const auto& d : store.QueryDaily(LocalDateOf(r.begin, tz), LocalDateOf(r.end - 1, tz))) {
            std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", d.day.year, d.day.month, d.day.day);
            AppendRow(body, buf, "day", d.series, d.adapterId, d.networkKey, d.rx, d.tx, lk);
        }
    }

    std::filesystem::path tmp = file;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(body.data(), static_cast<std::streamsize>(body.size()));
        f.flush();
        if (!f) {
            f.close();
            DeleteFileW(tmp.c_str());
            return false;
        }
    }
    if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace nsm
