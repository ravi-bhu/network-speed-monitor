#include "platform/settings.h"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <vector>

namespace nsm {
namespace {

constexpr const wchar_t* kGeneral = L"General";
constexpr const wchar_t* kReport = L"ReportWindow";
constexpr const wchar_t* kMissing = L"\x01<missing>";

std::optional<std::wstring> ReadString(const wchar_t* section, const wchar_t* key,
                                       const std::wstring& path) {
    std::vector<wchar_t> buf(256);
    for (;;) {
        DWORD n = GetPrivateProfileStringW(section, key, kMissing, buf.data(),
                                           static_cast<DWORD>(buf.size()), path.c_str());
        if (n + 1 >= buf.size()) {  // truncated, grow
            buf.resize(buf.size() * 2);
            continue;
        }
        std::wstring v(buf.data(), n);
        if (v == kMissing) return std::nullopt;
        return v;
    }
}

bool ReadBool(const wchar_t* key, const std::wstring& path, bool def) {
    auto v = ReadString(kGeneral, key, path);
    if (!v) return def;
    if (*v == L"1") return true;
    if (*v == L"0") return false;
    return def;
}

std::optional<LONG> ReadLong(const wchar_t* key, const std::wstring& path) {
    auto v = ReadString(kReport, key, path);
    if (!v || v->empty()) return std::nullopt;
    wchar_t* end = nullptr;
    errno = 0;
    long x = wcstol(v->c_str(), &end, 10);
    if (errno != 0 || end == v->c_str() || *end != L'\0') return std::nullopt;
    return static_cast<LONG>(x);
}

void AppendKey(std::wstring& out, const wchar_t* key, const std::wstring& value) {
    out += key;
    out += L'=';
    out += value;
    out += L"\r\n";
}

// Creates <ini>.<pid>.<n>.tmp exclusively, so concurrent saves never share a temp file.
HANDLE CreateTempFile(const std::filesystem::path& ini, std::wstring& tmpPath) {
    static std::atomic<unsigned> counter{0};
    for (int attempt = 0; attempt < 16; ++attempt) {
        std::filesystem::path tmp = ini;
        tmp += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(counter++) +
               L".tmp";
        tmpPath = tmp.wstring();
        HANDLE h = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) return h;
        DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) break;
        // The name is taken (a leftover from a save that crashed): try the next n.
    }
    return INVALID_HANDLE_VALUE;
}

}  // namespace

Settings LoadSettings(const std::filesystem::path& ini) {
    Settings s;
    const std::wstring path = ini.wstring();
    s.startWithWindows = ReadBool(L"StartWithWindows", path, s.startWithWindows);
    if (auto a = ReadString(kGeneral, L"Adapter", path)) s.adapterId = *a;
    if (auto u = ReadString(kGeneral, L"Units", path)) {
        if (*u == L"bits") s.units = UnitMode::Bits;
        else if (*u == L"bytes") s.units = UnitMode::Bytes;
    }
    s.showGraph = ReadBool(L"ShowGraph", path, s.showGraph);
    s.letters = ReadBool(L"Letters", path, s.letters);
    s.allTaskbars = ReadBool(L"AllTaskbars", path, s.allTaskbars);

    auto l = ReadLong(L"Left", path), t = ReadLong(L"Top", path), r = ReadLong(L"Right", path),
         b = ReadLong(L"Bottom", path);
    if (l && t && r && b && *r > *l && *b > *t) s.reportRect = RECT{*l, *t, *r, *b};
    return s;
}

bool SaveSettings(const std::filesystem::path& ini, const Settings& s) {
    // The file is written directly instead of with WritePrivateProfileString, whose cache flush
    // call has no usable return value, so a failed write could not be reported. The format is
    // what the profile API reads: UTF-16 with a BOM.
    std::wstring text = L"\xFEFF[General]\r\n";
    AppendKey(text, L"StartWithWindows", s.startWithWindows ? L"1" : L"0");
    AppendKey(text, L"Adapter", s.adapterId);
    AppendKey(text, L"Units", s.units == UnitMode::Bits ? L"bits" : L"bytes");
    AppendKey(text, L"ShowGraph", s.showGraph ? L"1" : L"0");
    AppendKey(text, L"Letters", s.letters ? L"1" : L"0");
    AppendKey(text, L"AllTaskbars", s.allTaskbars ? L"1" : L"0");
    if (s.reportRect) {
        const RECT& r = *s.reportRect;
        text += L"[ReportWindow]\r\n";
        AppendKey(text, L"Left", std::to_wstring(r.left));
        AppendKey(text, L"Top", std::to_wstring(r.top));
        AppendKey(text, L"Right", std::to_wstring(r.right));
        AppendKey(text, L"Bottom", std::to_wstring(r.bottom));
    }

    std::wstring tmpPath;
    HANDLE h = CreateTempFile(ini, tmpPath);
    if (h == INVALID_HANDLE_VALUE) return false;
    const DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    DWORD written = 0;
    bool ok = WriteFile(h, text.data(), bytes, &written, nullptr) != FALSE && written == bytes;
    ok = ok && FlushFileBuffers(h) != FALSE;
    ok = (CloseHandle(h) != FALSE) && ok;
    ok = ok && MoveFileExW(tmpPath.c_str(), ini.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(tmpPath.c_str());
    return ok;
}

}  // namespace nsm
