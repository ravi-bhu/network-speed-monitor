#include "platform/settings.h"

#include <cerrno>
#include <cstdio>
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

bool Write(const wchar_t* section, const wchar_t* key, const std::wstring& value,
           const std::wstring& path) {
    return WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()) != FALSE;
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
    std::filesystem::path tmp = ini;
    tmp += L".tmp";
    const std::wstring tmpPath = tmp.wstring();

    // Start from a fresh UTF-16 (BOM) file so the profile APIs store text as Unicode.
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, tmpPath.c_str(), L"wb") != 0 || !f) return false;
        const unsigned char bom[2] = {0xFF, 0xFE};
        bool ok = fwrite(bom, 1, 2, f) == 2;
        ok = (fclose(f) == 0) && ok;
        if (!ok) {
            DeleteFileW(tmpPath.c_str());
            return false;
        }
    }

    bool ok = Write(kGeneral, L"StartWithWindows", s.startWithWindows ? L"1" : L"0", tmpPath) &&
              Write(kGeneral, L"Adapter", s.adapterId, tmpPath) &&
              Write(kGeneral, L"Units", s.units == UnitMode::Bits ? L"bits" : L"bytes", tmpPath) &&
              Write(kGeneral, L"ShowGraph", s.showGraph ? L"1" : L"0", tmpPath) &&
              Write(kGeneral, L"Letters", s.letters ? L"1" : L"0", tmpPath) &&
              Write(kGeneral, L"AllTaskbars", s.allTaskbars ? L"1" : L"0", tmpPath);
    if (ok && s.reportRect) {
        const RECT& r = *s.reportRect;
        ok = Write(kReport, L"Left", std::to_wstring(r.left), tmpPath) &&
             Write(kReport, L"Top", std::to_wstring(r.top), tmpPath) &&
             Write(kReport, L"Right", std::to_wstring(r.right), tmpPath) &&
             Write(kReport, L"Bottom", std::to_wstring(r.bottom), tmpPath);
    }
    if (ok) {
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, tmpPath.c_str());  // flush cache
        ok = MoveFileExW(tmpPath.c_str(), ini.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) DeleteFileW(tmpPath.c_str());
    return ok;
}

}  // namespace nsm
