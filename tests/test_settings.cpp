#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <atomic>
#include <filesystem>
#include <fstream>

#include "doctest.h"
#include "platform/autostart.h"
#include "platform/paths.h"
#include "platform/settings.h"

using namespace nsm;

namespace {

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
        std::filesystem::remove(path, ec);
        std::filesystem::remove(path.wstring() + L".tmp", ec);
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
    CHECK(!std::filesystem::exists(t.path.wstring() + L".tmp"));
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
    CHECK(SetAutostart(false, name));
    CHECK(!IsAutostartEnabled(name));
    CHECK(SetAutostart(false, name));  // missing value is success
}
