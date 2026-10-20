#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "doctest.h"
#include "platform/autostart.h"
#include "platform/paths.h"
#include "platform/settings.h"

using namespace nsm;

namespace {

// Any <ini>.*.tmp next to the file, i.e. a save's temp file.
std::vector<std::filesystem::path> TempFilesOf(const std::filesystem::path& ini) {
    std::vector<std::filesystem::path> out;
    const std::wstring prefix = ini.filename().wstring() + L".";
    for (const auto& e : std::filesystem::directory_iterator(ini.parent_path())) {
        const std::wstring n = e.path().filename().wstring();
        if (n.starts_with(prefix) && n.ends_with(L".tmp")) out.push_back(e.path());
    }
    return out;
}

std::wstring ReadUtf16(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::wstring w(bytes.size() / sizeof(wchar_t), L'\0');
    std::memcpy(w.data(), bytes.data(), w.size() * sizeof(wchar_t));
    return w;
}

struct TempIni {
    std::filesystem::path path;
    TempIni() {
        static std::atomic<int> n{0};
        path = std::filesystem::temp_directory_path() /
               (L"nsm_settings_test_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                std::to_wstring(n++) + L".ini");
        std::filesystem::remove(path);
    }
    ~TempIni() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        for (const auto& tmp : TempFilesOf(path)) std::filesystem::remove(tmp, ec);
    }
    void Write(const char* text) { std::ofstream(path, std::ios::binary) << text; }
};

}  // namespace

TEST_CASE("Settings round trip of non-default values") {
    TempIni t;
    Settings s;
    s.startWithWindows = false;
    s.adapterId = L"{12345678-ABCD-4321-8765-0123456789AB}";
    s.units = UnitMode::Bits;
    s.showGraph = false;
    s.letters = true;
    s.allTaskbars = true;
    s.reportRect = RECT{10, -20, 810, 620};
    REQUIRE(SaveSettings(t.path, s));
    CHECK(LoadSettings(t.path) == s);
    CHECK(TempFilesOf(t.path).empty());
}

TEST_CASE("Settings without report rect round trips and overwrites") {
    TempIni t;
    Settings a;
    a.reportRect = RECT{1, 2, 3, 4};
    REQUIRE(SaveSettings(t.path, a));
    Settings b;  // no rect, defaults
    REQUIRE(SaveSettings(t.path, b));
    CHECK(LoadSettings(t.path) == b);
    CHECK(!LoadSettings(t.path).reportRect);
}

TEST_CASE("Settings round trip keeps a non-ASCII adapter id") {
    TempIni t;
    Settings s;
    s.adapterId = L"Ethernet é中";
    REQUIRE(SaveSettings(t.path, s));
    CHECK(LoadSettings(t.path).adapterId == s.adapterId);
}

TEST_CASE("Settings missing file gives defaults") {
    TempIni t;
    CHECK(LoadSettings(t.path) == Settings{});
}

TEST_CASE("Settings invalid values fall back per key, others still parse") {
    TempIni t;
    t.Write("[General]\r\nUnits=furlongs\r\nShowGraph=maybe\r\nLetters=1\r\nAdapter=abc\r\n"
            "StartWithWindows=0\r\n");
    Settings s = LoadSettings(t.path);
    CHECK(s.units == UnitMode::Bytes);
    CHECK(s.showGraph == true);
    CHECK(s.letters == true);
    CHECK(s.adapterId == L"abc");
    CHECK(s.startWithWindows == false);
}

TEST_CASE("Settings report rect needs all four keys and positive size") {
    TempIni t;
    t.Write("[ReportWindow]\r\nLeft=0\r\nTop=0\r\nRight=100\r\n");
    CHECK(!LoadSettings(t.path).reportRect);
    t.Write("[ReportWindow]\r\nLeft=0\r\nTop=0\r\nRight=0\r\nBottom=100\r\n");
    CHECK(!LoadSettings(t.path).reportRect);
    t.Write("[ReportWindow]\r\nLeft=0\r\nTop=50\r\nRight=100\r\nBottom=50\r\n");
    CHECK(!LoadSettings(t.path).reportRect);
    t.Write("[ReportWindow]\r\nLeft=0\r\nTop=x\r\nRight=100\r\nBottom=100\r\n");
    CHECK(!LoadSettings(t.path).reportRect);
    t.Write("[ReportWindow]\r\nLeft=5\r\nTop=6\r\nRight=100\r\nBottom=101\r\n");
    auto r = LoadSettings(t.path).reportRect;
    REQUIRE(r.has_value());
    CHECK(r->left == 5);
    CHECK(r->bottom == 101);
}

TEST_CASE("SaveSettings fails when the directory does not exist") {
    TempIni t;
    CHECK(!SaveSettings(t.path.parent_path() / L"nsm_no_such_dir_x" / L"s.ini", Settings{}));
}

TEST_CASE("SaveSettings writes a UTF-16 INI with a BOM") {
    TempIni t;
    Settings s;
    s.adapterId = L"{A}";
    s.reportRect = RECT{1, 2, 3, 4};
    REQUIRE(SaveSettings(t.path, s));
    CHECK(ReadUtf16(t.path) ==
          L"\xFEFF[General]\r\nStartWithWindows=1\r\nAdapter={A}\r\nUnits=bytes\r\n"
          L"ShowGraph=1\r\nLetters=0\r\nAllTaskbars=0\r\n"
          L"[ReportWindow]\r\nLeft=1\r\nTop=2\r\nRight=3\r\nBottom=4\r\n");
}

TEST_CASE("SaveSettings is not blocked by another writer's temp file") {
    TempIni t;
    // A second process mid-save of the same file, holding its temp file open.
    const std::filesystem::path other = t.path.wstring() + L".tmp";
    HANDLE h = CreateFileW(other.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    Settings s;
    s.letters = true;
    CHECK(SaveSettings(t.path, s));
    CHECK(SaveSettings(t.path, s));
    CloseHandle(h);
    CHECK(LoadSettings(t.path) == s);
    CHECK(TempFilesOf(t.path) == std::vector<std::filesystem::path>{other});
}

TEST_CASE("SaveSettings failing to replace the file reports false and cleans up") {
    TempIni t;
    std::filesystem::create_directory(t.path);  // a directory can't be replaced by a file
    CHECK(!SaveSettings(t.path, Settings{}));
    CHECK(TempFilesOf(t.path).empty());
}

TEST_CASE("DataDir is NetworkSpeedMonitor under LocalAppData" * doctest::test_suite("integration")) {
    auto dir = DataDir();
    REQUIRE(!dir.empty());
    CHECK(dir.filename() == L"NetworkSpeedMonitor");
    PWSTR raw = nullptr;
    REQUIRE(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)));
    std::filesystem::path local = raw;
    CoTaskMemFree(raw);
    CHECK(dir.parent_path() == local);
    CHECK(std::filesystem::is_directory(dir));
}

TEST_CASE("Autostart set and clear" * doctest::test_suite("integration")) {
    const std::wstring name = L"NetworkSpeedMonitorTest";
    struct Guard {
        const std::wstring& n;
        ~Guard() { SetAutostart(false, n); }
    } guard{name};
    SetAutostart(false, name);
    CHECK(!IsAutostartEnabled(name));
    CHECK(SetAutostart(true, name));
    CHECK(IsAutostartEnabled(name));
    {
        std::vector<wchar_t> exe(32768);
        DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        REQUIRE((n > 0 && n < exe.size()));
        std::vector<wchar_t> data(exe.size() + 2);
        DWORD size = static_cast<DWORD>(data.size() * sizeof(wchar_t));
        REQUIRE(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                             name.c_str(), RRF_RT_REG_SZ, nullptr, data.data(),
                             &size) == ERROR_SUCCESS);
        CHECK(std::wstring(data.data()) == L"\"" + std::wstring(exe.data(), n) + L"\"");
    }
    CHECK(SetAutostart(false, name));
    CHECK(!IsAutostartEnabled(name));
    CHECK(SetAutostart(false, name));  // missing value is success
}
