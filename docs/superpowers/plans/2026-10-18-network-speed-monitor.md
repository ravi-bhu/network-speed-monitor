# Network Speed Monitor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A tiny native Windows app that draws live up/down speed on the taskbar and records data usage, with a tabbed report window, shipped as a per-user installer and via winget.

**Architecture:** A single-threaded Win32 app. A hidden message window runs a 1-second timer that reads every adapter's 64-bit counters. A pure `Sampler` turns those readings into (a) a display speed for the overlay and (b) per-adapter plus "internet" usage deltas. The deltas are batched in memory and written to SQLite every ≤5 minutes. The overlay is a per-pixel-alpha layered window kept glued to the taskbar. On Windows 10 it also reserves space in the task list. The report window reads SQLite through a pure `ReportModel`. All logic that can be pure lives in `nsm_core` / `nsm_report` and is unit-tested; Win32 glue is verified with integration tests and a manual checklist.

**Tech Stack:** C++20, MSVC (VS 2022 Build Tools), CMake ≥ 3.25, Win32 + Direct2D/DirectWrite, IP Helper API, Network List Manager (COM), UI Automation, SQLite amalgamation (vendored), doctest (vendored, tests only), Inno Setup 6, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-18-network-speed-monitor-design.md`. Read it before starting any task. Section numbers below (§) refer to it.

## Global Constraints

- C++20, MSVC, CMake ≥ 3.25, **static CRT (`/MT`, `/MTd`)**, so there's no runtime to install.
- Win32 only. Drawing uses GDI/Direct2D/DirectWrite. No WebView, .NET, Qt, Electron, MFC/ATL dependencies, or chart libraries.
- Windows 10 and Windows 11. Build x64 and ARM64; tests run on x64.
- Standard user only. The manifest uses `requestedExecutionLevel level="asInvoker"`. Never request elevation.
- **The app makes zero network connections.** The exe must never import `ws2_32.dll`, `winhttp.dll`, `wininet.dll` or `urlmon.dll` (enforced in CI, Task 18).
- Bundled third-party code: SQLite (public domain), compiled into the exe. doctest (MIT) is used only in tests.
- All user-visible strings live in `res/strings.rc` (`STRINGTABLE`) and are loaded with `nsm::LoadStr(UINT id)`. Unit suffixes (`KB/s`, `Mbps`, `GB`…) are the only literals allowed in code.
- Units are decimal (1 KB = 1000 B). The default unit is bytes. Report volumes are always bytes.
- Exe name `NetworkSpeedMonitor.exe`; product name "Network Speed Monitor".
- Data folder `%LOCALAPPDATA%\NetworkSpeedMonitor\`, containing `usage.db` and `settings.ini`.
- Per-monitor DPI awareness v2 (manifest), and Common Controls v6.
- Speed refresh 1 s; mini graph keeps 60 samples; DB flush every ≤ 300 s; hourly rows kept 90 days, then rolled up to daily.
- Budgets with only the overlay showing: < 0.5% CPU averaged, < 15 MB private working set.
- Every commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Adapter vanishes and returns with reset counters** (USB Wi-Fi replug, VPN reconnect). Expected: no spike and no negative or huge delta; the adapter resumes from a fresh baseline. → Task 9 test `Sampler: adapter that disappears and returns starts fresh`.
2. **The internet adapter changes mid-hour** (docking switches Wi-Fi → Ethernet). Expected: each tick's internet delta is attributed to whichever adapter carried it at that tick. → Task 9 test `Sampler: internet series follows the internet adapter per tick`.
3. **Long Modern Standby gap with a large but plausible delta.** Expected: counted toward usage, no speed spike on the taskbar. → Task 2 test `CounterTracker: long gap counts usage but not speed`.
4. **No internet adapter at all** (airplane mode, first boot offline). Expected: overlay shows `—`, nothing crashes, and recording continues for any adapter that moves bytes. → Task 9 test `Sampler: no internet adapter gives no display speed`.
5. **Adapter/network names containing commas, quotes or non-ASCII characters** in CSV export. Expected: valid CSV that opens correctly in Excel. → Task 14 tests `CsvEscape quotes…` and `ExportCsv writes UTF-8 with BOM`.

---

## File Structure

```
CMakeLists.txt                     root: options, /MT, subdirs, NSM_SOURCE_URL cache var
src/core/        (nsm_core: no OS side effects; unit-tested)
  types.h                          AdapterKind, Series, LocalDate, UtcRange, Speed, UsageRow, DailyRow
  units.h/.cpp                     FormatSpeed, FormatVolume
  counter_tracker.h/.cpp           reset/gap/plausibility logic for one adapter
  sampler.h/.cpp                   all adapters per tick -> deltas + display speed
  speed_history.h/.cpp             60-sample ring buffer for the mini graph
  overlay_layout.h/.cpp            pure placement geometry, auto-hide & fullscreen predicates
  time_zone.h/.cpp                 UTC <-> local via DYNAMIC_TIME_ZONE_INFORMATION
  periods.h/.cpp                   Period -> UtcRange, hour/day helpers
  usage_accumulator.h/.cpp         in-memory hourly buckets + flush policy
src/platform/    (nsm_platform: Win32 wrappers)
  adapters.h/.cpp                  enumerate/classify adapters, read counters, monotonic clock
  internet_route.h/.cpp            FindInternetInterface, NetChangeWatcher
  network_identity.h/.cpp          adapter -> NLM network (key, name)
  settings.h/.cpp                  Settings INI load/save
  autostart.h/.cpp                 HKCU Run value
  paths.h/.cpp                     DataDir()
  theme.h/.cpp                     taskbar/app light-dark, high contrast
  strings.h/.cpp                   LoadStr
src/storage/     (nsm_storage)
  usage_store.h/.cpp               SQLite schema, upsert, query, rollup, clear, corruption recovery
src/report/      (nsm_report)
  report_model.h/.cpp              Summary/Timeline/Adapters/Wi-Fi data
  csv_export.h/.cpp                CSV writer
src/ui/          (NetworkSpeedMonitor.exe)
  main.cpp                         wWinMain -> App::Run
  app.h/.cpp                       message window, timer, wiring, menu, power, single instance
  taskbar_probe.h/.cpp             find taskbars, tray rect, occupied extent (UIA)
  taskbar_space_win10.h/.cpp       Win10 task-list space reservation
  overlay_renderer.h/.cpp          D2D/DWrite drawing of layout C
  overlay_window.h/.cpp            one layered window per taskbar
  overlay_manager.h/.cpp           lifecycle: TaskbarCreated, autohide, fullscreen, z-order, monitors
  report_window.h/.cpp             tabbed report window
  report_paint.h/.cpp              cards + bar chart painting
  dialogs.h/.cpp                   settings, rename, custom range, confirm
res/
  app.rc, resource.h, strings.rc, app.manifest, app.ico
third_party/sqlite/ (sqlite3.c, sqlite3.h, VERSION)   third_party/doctest/doctest.h
tests/  CMakeLists.txt, test_main.cpp, test_*.cpp
installer/NetworkSpeedMonitor.iss
packaging/winget/ (manifest templates)
tools/check_imports.ps1, tools/measure.ps1
.github/workflows/ci.yml, release.yml
LICENSE README.md PRIVACY.md CONTRIBUTING.md SECURITY.md THIRD_PARTY_NOTICES.md docs/signing.md
```

Build and test commands used throughout (PowerShell, repo root):

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
build\tests\Debug\nsm_tests.exe -tc="<test name pattern>"
```

---

### Task 1: Toolchain, scaffolding, CI and unit formatting

**Files:**
- Create: `CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/core/types.h`, `src/core/units.h`, `src/core/units.cpp`, `tests/CMakeLists.txt`, `tests/test_main.cpp`, `tests/test_units.cpp`, `third_party/doctest/doctest.h`, `.gitignore`, `LICENSE`, `.github/workflows/ci.yml`

**Interfaces:**
- Produces (`src/core/types.h`, namespace `nsm`):
  ```cpp
  enum class AdapterKind { WiFi, Ethernet, Virtual, Other };
  enum class Series { Adapter = 0, Internet = 1 };
  enum class UnitMode { Bits, Bytes };
  struct Speed { double rx = 0, tx = 0; };                       // bytes per second
  struct UtcRange { int64_t begin = 0, end = 0; };               // [begin, end), unix seconds
  struct LocalDate { int year = 0, month = 0, day = 0; auto operator<=>(const LocalDate&) const = default; };
  struct UsageRow { int64_t hourUtc; Series series; std::wstring adapterId; std::wstring networkKey; uint64_t rx, tx; };
  struct DailyRow { LocalDate day; Series series; std::wstring adapterId; std::wstring networkKey; uint64_t rx, tx; };
  ```
- Produces (`src/core/units.h`): `std::wstring FormatSpeed(double bytesPerSec, UnitMode mode);` `std::wstring FormatVolume(uint64_t bytes);`

- [ ] **Step 1: Install prerequisites (user action, needs their OK).** The machine currently has no VS/CMake. Ask the user to run:
  `winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"`
  (includes MSVC, the Windows SDK and CMake). Verify in a new "Developer PowerShell for VS 2022": `cmake --version` prints ≥ 3.25, and `cl` prints a version banner.

- [ ] **Step 2: Scaffold.** Root `CMakeLists.txt`: `project(NetworkSpeedMonitor VERSION 0.1.0 LANGUAGES C CXX)`, `CMAKE_CXX_STANDARD 20`, `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"`, `/W4 /permissive- /utf-8`, defines `UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00`, cache var `NSM_SOURCE_URL` (default `""`), `enable_testing()`, subdirs `src/core tests`. Add later subdirs as tasks create them. Vendor doctest 2.4.x single header. `tests/test_main.cpp` defines `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`. `.gitignore`: `build/`, `out/`, `.vs/`, `.superpowers/`. `LICENSE`: MIT, "Copyright (c) 2026 Ravi Bhushan".

- [ ] **Step 3: Write the failing tests** in `tests/test_units.cpp`:
  ```cpp
  TEST_CASE("FormatSpeed bytes auto-scales to ~3 significant figures") {
    CHECK(FormatSpeed(0, UnitMode::Bytes) == L"0 B/s");
    CHECK(FormatSpeed(999, UnitMode::Bytes) == L"999 B/s");
    CHECK(FormatSpeed(1000, UnitMode::Bytes) == L"1.00 KB/s");
    CHECK(FormatSpeed(950'000, UnitMode::Bytes) == L"950 KB/s");
    CHECK(FormatSpeed(1'240'000, UnitMode::Bytes) == L"1.24 MB/s");
    CHECK(FormatSpeed(15'800'000, UnitMode::Bytes) == L"15.8 MB/s");
    CHECK(FormatSpeed(999'600, UnitMode::Bytes) == L"1.00 MB/s");   // rounding crosses a unit boundary
    CHECK(FormatSpeed(1'250'000'000, UnitMode::Bytes) == L"1.25 GB/s");
    CHECK(FormatSpeed(2e12, UnitMode::Bytes) == L"2000 GB/s");      // GB is the largest unit
    CHECK(FormatSpeed(-5, UnitMode::Bytes) == L"0 B/s");
  }
  TEST_CASE("FormatSpeed bits multiplies by 8 and uses bps units") {
    CHECK(FormatSpeed(100, UnitMode::Bits) == L"800 bps");
    CHECK(FormatSpeed(125, UnitMode::Bits) == L"1.00 Kbps");
    CHECK(FormatSpeed(1'240'000, UnitMode::Bits) == L"9.92 Mbps");
    CHECK(FormatSpeed(1'250'000'000, UnitMode::Bits) == L"10.0 Gbps");
  }
  TEST_CASE("FormatVolume uses decimal byte units up to TB") {
    CHECK(FormatVolume(0) == L"0 B");
    CHECK(FormatVolume(999) == L"999 B");
    CHECK(FormatVolume(64'700'000'000ull) == L"64.7 GB");
    CHECK(FormatVolume(1'500'000'000'000ull) == L"1.50 TB");
  }
  ```

- [ ] **Step 4: Run and confirm failure.** `cmake --build build --config Debug` fails to compile (`FormatSpeed` undeclared).

- [ ] **Step 5: Implement `units.cpp`.** Rule: values below 1000 in the base unit (`B/s`, `bps`, `B`) are integers. Otherwise pick the largest unit where value ≥ 1, show 2 decimals if < 10, 1 decimal if < 100, else 0. If rounding yields 1000, step up a unit. Clamp at the largest unit (`GB/s`, `Gbps`, `TB`). NaN or negative → 0.

- [ ] **Step 6: Run the tests.** `build\tests\Debug\nsm_tests.exe -tc="Format*"` → all pass.

- [ ] **Step 7: CI.** `.github/workflows/ci.yml` on push/PR, `windows-2022`: configure x64, build Debug + Release, run `build\tests\Release\nsm_tests.exe -tse="integration"` (unit tests only), then run the integration suite as a separate step with `continue-on-error: false`. Also configure and build ARM64 Release (`-A ARM64`) without running tests.

- [ ] **Step 8: Commit.** `git add -A && git commit -m "build: scaffold CMake project, CI, and speed/volume formatting"`

---

### Task 2: CounterTracker (resets, gaps, plausibility)

**Files:**
- Create: `src/core/counter_tracker.h`, `src/core/counter_tracker.cpp`, `tests/test_counter_tracker.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct CounterReading { uint64_t rxBytes; uint64_t txBytes; uint64_t linkBitsPerSec; double timeSec; };
  struct TrackerResult { bool speedValid = false; Speed speed; uint64_t rxDelta = 0, txDelta = 0; };
  inline constexpr double kGapSeconds = 3.0;
  inline constexpr uint64_t kUnknownLinkBitsPerSec = 100'000'000'000ull;
  class CounterTracker { public: TrackerResult Update(const CounterReading& r); };
  ```

- [ ] **Step 1: Write the failing tests** (`tests/test_counter_tracker.cpp`). Abbreviation: `R(rx, tx, link, t)` builds a `CounterReading`.
  ```cpp
  TEST_CASE("CounterTracker: first reading is a baseline only") {
    CounterTracker t; auto r = t.Update(R(500, 500, 1'000'000'000, 0.0));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 0); CHECK(r.txDelta == 0);
  }
  TEST_CASE("CounterTracker: normal second gives speed and deltas") {
    CounterTracker t; t.Update(R(0, 0, 1'000'000'000, 10.0));
    auto r = t.Update(R(1'000'000, 500'000, 1'000'000'000, 10.5));
    CHECK(r.speedValid); CHECK(r.speed.rx == doctest::Approx(2'000'000)); CHECK(r.speed.tx == doctest::Approx(1'000'000));
    CHECK(r.rxDelta == 1'000'000); CHECK(r.txDelta == 500'000);
  }
  TEST_CASE("CounterTracker: counter going backwards resets baseline") {
    CounterTracker t; t.Update(R(9'000, 9'000, 1'000'000'000, 0)); auto r = t.Update(R(100, 100, 1'000'000'000, 1));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 0);
    auto n = t.Update(R(1'100, 600, 1'000'000'000, 2)); CHECK(n.speedValid); CHECK(n.rxDelta == 1'000); CHECK(n.txDelta == 500);
  }
  TEST_CASE("CounterTracker: implausible delta beyond link speed is dropped") {
    CounterTracker t; t.Update(R(0, 0, 100'000'000, 0));                // 100 Mbps = 12.5 MB/s
    auto r = t.Update(R(50'000'000, 0, 100'000'000, 1));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 0);
    auto n = t.Update(R(51'000'000, 0, 100'000'000, 2)); CHECK(n.rxDelta == 1'000'000);  // baseline moved
  }
  TEST_CASE("CounterTracker: long gap counts usage but not speed") {
    CounterTracker t; t.Update(R(0, 0, 1'000'000'000, 0));
    auto r = t.Update(R(2'000'000'000, 30'000'000, 1'000'000'000, 3600));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 2'000'000'000); CHECK(r.txDelta == 30'000'000);
  }
  TEST_CASE("CounterTracker: unknown link speed uses 100 Gbps cap") {
    CounterTracker t; t.Update(R(0, 0, 0, 0));
    CHECK(t.Update(R(5'000'000'000, 0, 0, 1)).rxDelta == 5'000'000'000);
  }
  TEST_CASE("CounterTracker: non-increasing time is ignored") {
    CounterTracker t; t.Update(R(0, 0, 1'000'000'000, 5)); auto r = t.Update(R(1'000, 0, 1'000'000'000, 5));
    CHECK(!r.speedValid); CHECK(r.rxDelta == 0);
    CHECK(t.Update(R(2'000, 0, 1'000'000'000, 6)).rxDelta == 2'000);   // baseline was not moved
  }
  ```
- [ ] **Step 2: Run and confirm failure** (`-tc="CounterTracker*"` → compile error).
- [ ] **Step 3: Implement `CounterTracker::Update`.** Rules in order. (1) No previous reading: store it as the baseline and return the default result. (2) `dt <= 0`: return the default result without moving the baseline. (3) Either counter went backwards: move the baseline, return the default result. (4) The cap is `(link ? link : kUnknownLinkBitsPerSec) / 8.0 * dt * 1.1` bytes; if either delta exceeds it, move the baseline and return the default result. (5) If `dt > kGapSeconds`, return deltas with `speedValid=false`. (6) Otherwise return deltas and speed = delta / dt. Move the baseline in cases 3–6.
- [ ] **Step 4: Run the tests** → all pass.
- [ ] **Step 5: Commit** `feat(core): counter tracker with reset, gap and plausibility handling`

---

### Task 3: Pure overlay logic (history buffer, placement, auto-hide, fullscreen)

**Files:**
- Create: `src/core/speed_history.h/.cpp`, `src/core/overlay_layout.h/.cpp`, `tests/test_speed_history.cpp`, `tests/test_overlay_layout.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class SpeedHistory { public: explicit SpeedHistory(size_t capacity = 60); void Push(Speed s); size_t Size() const;
    double Max() const;               // max over rx and tx, never below 1000.0
    std::vector<double> Rx() const; std::vector<double> Tx() const; };   // oldest first
  enum class Edge { Bottom, Top, Left, Right };
  struct Rect { int left = 0, top = 0, right = 0, bottom = 0; int Width() const; int Height() const; bool Empty() const; bool operator==(const Rect&) const = default; };
  struct TaskbarGeometry { Rect monitor, taskbar, tray, occupied; Edge edge; unsigned dpi; };
  struct OverlaySizes { int fullWidth, textOnlyWidth, height; };              // physical px at geometry.dpi
  struct OverlayPlacement { bool visible = false; bool showGraph = false; Rect rect; };
  OverlayPlacement PlaceOverlay(const TaskbarGeometry& g, const OverlaySizes& s, bool wantGraph);
  bool IsTaskbarShown(const Rect& taskbar, const Rect& monitor, Edge edge);
  bool IsFullscreenOnMonitor(const Rect& window, const Rect& monitor, bool windowIsShell);
  ```

- [ ] **Step 1: Write the failing tests.**
  ```cpp
  TEST_CASE("SpeedHistory keeps the newest 60 samples, oldest first") {
    SpeedHistory h; for (int i = 0; i < 65; ++i) h.Push({double(i * 1000), 0});
    CHECK(h.Size() == 60); CHECK(h.Rx().front() == 5000); CHECK(h.Rx().back() == 64000); CHECK(h.Max() == 64000);
  }
  TEST_CASE("SpeedHistory Max floors at 1000") { SpeedHistory h; h.Push({10, 20}); CHECK(h.Max() == 1000); }

  // Bottom taskbar 1920x1080 @96dpi: taskbar {0,1040,1920,1080}, tray {1700,1040,1920,1080}, sizes {150, 90, 40}
  TEST_CASE("PlaceOverlay: room for graph sits left of tray with 4px margin") {
    auto p = PlaceOverlay(G(Edge::Bottom, {0,1040,1000,1080}), {150, 90, 40}, true);
    CHECK(p.visible); CHECK(p.showGraph); CHECK(p.rect == Rect{1546,1040,1696,1080});
  }
  TEST_CASE("PlaceOverlay: crowded drops graph, then hides") {
    auto a = PlaceOverlay(G(Edge::Bottom, {0,1040,1560,1080}), {150, 90, 40}, true);
    CHECK(a.visible); CHECK(!a.showGraph); CHECK(a.rect == Rect{1606,1040,1696,1080});
    auto b = PlaceOverlay(G(Edge::Bottom, {0,1040,1620,1080}), {150, 90, 40}, true);
    CHECK(!b.visible);
  }
  TEST_CASE("PlaceOverlay: margin scales with DPI") {   // dpi 144 -> margin 6
    auto g = G(Edge::Bottom, {}); g.dpi = 144;
    CHECK(PlaceOverlay(g, {150, 90, 40}, false).rect.right == 1694);
  }
  TEST_CASE("PlaceOverlay: vertical taskbar stacks above tray, text only when narrow") {
    TaskbarGeometry g{{0,0,1920,1080}, {0,0,62,1080}, {0,900,62,1080}, {0,0,62,400}, Edge::Left, 96};
    auto p = PlaceOverlay(g, {150, 54, 40}, true);
    CHECK(p.visible); CHECK(!p.showGraph); CHECK(p.rect == Rect{4,856,58,896});
  }
  TEST_CASE("IsTaskbarShown detects auto-hidden taskbar") {
    CHECK(IsTaskbarShown({0,1040,1920,1080}, {0,0,1920,1080}, Edge::Bottom));
    CHECK(!IsTaskbarShown({0,1078,1920,1118}, {0,0,1920,1080}, Edge::Bottom));
  }
  TEST_CASE("IsFullscreenOnMonitor: covers monitor, not maximized, not shell") {
    CHECK(IsFullscreenOnMonitor({0,0,1920,1080}, {0,0,1920,1080}, false));
    CHECK(!IsFullscreenOnMonitor({-8,-8,1928,1048}, {0,0,1920,1080}, false));
    CHECK(!IsFullscreenOnMonitor({0,0,1920,1080}, {0,0,1920,1080}, true));
  }
  ```
  `G(edge, occupied)` is a test helper returning the bottom-taskbar geometry above with the given `occupied` rect.
- [ ] **Step 2: Run and confirm failure.**
- [ ] **Step 3: Implement.** `margin = MulDiv(4, dpi, 96)`. **Horizontal:** right edge = `tray.left - margin`; free space starts at `occupied.Empty() ? taskbar.left : occupied.right + margin`; use the graph if `wantGraph && fullWidth <= free`, else text-only if `textOnlyWidth <= free`, else not visible; centre vertically in the taskbar. **Vertical:** width = `taskbar.Width() - 2*margin`; not visible if `width < textOnlyWidth`; graph only if `wantGraph && width >= fullWidth` (height doubles, graph stacked above text); bottom = `tray.top - margin`; not visible if the height doesn't fit above `occupied.bottom + margin`. `IsTaskbarShown`: the visible thickness inside the monitor along the edge axis is ≥ half the taskbar's thickness. `IsFullscreenOnMonitor`: `!windowIsShell` and the window rect contains the monitor rect.
- [ ] **Step 4: Run the tests** → pass.
- [ ] **Step 5: Commit** `feat(core): overlay placement geometry and speed history`

---

### Task 4: Overlay window on the taskbar (demo data), de-risking first

The spec (§13.1) says to spike this first. This task produces a real, kept overlay driven by fake data (`--demo`).

**Files:**
- Create: `src/platform/CMakeLists.txt`, `src/platform/theme.h/.cpp`, `src/platform/strings.h/.cpp`, `src/ui/CMakeLists.txt`, `src/ui/main.cpp`, `src/ui/taskbar_probe.h/.cpp`, `src/ui/overlay_renderer.h/.cpp`, `src/ui/overlay_window.h/.cpp`, `res/app.rc`, `res/resource.h`, `res/strings.rc`, `res/app.manifest`, `res/app.ico`

**Interfaces:**
- Consumes: `SpeedHistory`, `PlaceOverlay`, `OverlaySizes`, `TaskbarGeometry`, `Rect`, `FormatSpeed` (Tasks 1, 3).
- Produces:
  ```cpp
  // platform/theme.h
  bool IsTaskbarLight();      // HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\SystemUsesLightTheme == 1
  bool AreAppsLight();        // ...\AppsUseLightTheme == 1
  bool IsHighContrast();      // SPI_GETHIGHCONTRAST
  // platform/strings.h
  std::wstring LoadStr(UINT id);
  // ui/taskbar_probe.h
  struct TaskbarInfo { HWND hwnd; HMONITOR monitor; bool primary; TaskbarGeometry geom; };
  std::vector<TaskbarInfo> ProbeTaskbars();     // Shell_TrayWnd + every Shell_SecondaryTrayWnd
  bool IsWindows11();                           // build >= 22000 via RtlGetVersion
  // ui/overlay_renderer.h
  struct OverlayContent { std::optional<Speed> speed; UnitMode units; bool letters; bool showGraph; const SpeedHistory* history; bool light; unsigned dpi; };
  OverlaySizes MeasureOverlay(unsigned dpi, bool letters);
  void RenderOverlay(ID2D1DCRenderTarget* rt, SIZE size, const OverlayContent& c);
  // ui/overlay_window.h
  class OverlayWindow { public:
    bool Create(HINSTANCE inst, HWND owner); void Show(const OverlayPlacement& p, const OverlayContent& c); void Hide(); HWND Hwnd() const;
    std::function<void()> onLeftClick; std::function<void(POINT screenPt)> onRightClick; };
  ```

- [ ] **Step 1: Taskbar probe.** The tray rect is `TrayNotifyWnd` on the primary taskbar. On secondary taskbars, use the clock child if found (`ClockButton` on Win10; on Win11 via UIA), otherwise a zero-width rect at the taskbar's far end. `edge` comes from `SHAppBarMessage(ABM_GETTASKBARPOS)` (primary) or by comparing the rect to the monitor (secondary). **Occupied extent:** use UI Automation (`IUIAutomation::ElementFromHandle(taskbar)` → `FindAll(TreeScope_Descendants, ControlType Button)`), and take the union of bounding rects that lie entirely before the tray (left of `tray.left`, or above `tray.top` when vertical). Cache the `IUIAutomation` instance.
- [ ] **Step 2: Renderer.** Draw into a 32bpp top-down DIB via `ID2D1DCRenderTarget` (premultiplied alpha). The font is "Segoe UI Variable Text" if installed, else "Segoe UI", 12 DIP, with the DWrite tabular-figures feature (`DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES`). Two right-aligned lines: upload on top, download below. Arrows `↑`/`↓` are drawn in the series color; in letters mode, `U:`/`D:` are drawn in the muted color. `speed == nullopt` → value `—`. The graph is 54×28 DIP, 6 DIP left of the text, with two 1.5 DIP polylines scaled to `history->Max()`. Colors: dark taskbar uses text `#FFFFFF`, upload `#F0A35E`, download `#5EB8F0`, muted `#9A9A9A`; light taskbar uses text `#000000`, upload `#B35C00`, download `#0067C0`, muted `#5F5F5F`. Fill the background with alpha 1/255 so the whole rect is clickable. `MeasureOverlay` measures the widest plausible strings (`↑ 888 KB/s`, `↓ 888 Kbps`, plus the letters variants) at that DPI.
- [ ] **Step 3: Overlay window.** Use `WS_POPUP`, with `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE`, updated via `UpdateLayeredWindow` with per-pixel alpha. `WM_MOUSEACTIVATE` → `MA_NOACTIVATE`. A left button up fires `onLeftClick`; a right button up fires `onRightClick(screen point)`.
- [ ] **Step 4: Demo entry point.** `main.cpp`: `wWinMain` with `--demo` creates one `OverlayWindow` on the primary taskbar. A 1 s timer pushes sine-wave fake speeds into a `SpeedHistory`, re-probes geometry every 2 s, and calls `PlaceOverlay` + `Show`. Left-click toggles the graph; right-click toggles letters, which exercises both toggles.
- [ ] **Step 5: Manual verification on Windows 10 (this machine), and record the results in the commit message:**
  - Run `build\src\ui\Debug\NetworkSpeedMonitor.exe --demo`. The overlay appears immediately left of the tray, with no overlap, and both lines are readable.
  - Switch Windows mode light ↔ dark (Settings > Personalization > Colors). Restart the demo; the colors match the taskbar.
  - At 150% scaling, the text is crisp with no blur.
  - Open about 20 windows so the task buttons approach the tray. The graph drops, then the overlay hides, and it never overlaps.
  - Task Manager shows < 15 MB private working set.
- [ ] **Step 6: Windows 11 check.** Repeat Step 5 in a Windows 11 VM (Hyper-V "Windows 11 dev environment" image), with the taskbar centered and left-aligned. If UIA fails to report Win11 task buttons, stop and report back to the user before continuing; this is the spec's top risk.
- [ ] **Step 7: Commit** `feat(ui): taskbar overlay window with demo mode`

---

### Task 5: Overlay lifecycle (Explorer restart, auto-hide, fullscreen, z-order, multi-monitor, Win10 space)

**Files:**
- Create: `src/ui/overlay_manager.h/.cpp`, `src/ui/taskbar_space_win10.h/.cpp`
- Modify: `src/ui/main.cpp` (demo uses `OverlayManager`)

**Interfaces:**
- Consumes: `ProbeTaskbars`, `OverlayWindow`, `MeasureOverlay`, `PlaceOverlay`, `IsTaskbarShown`, `IsFullscreenOnMonitor`, `IsTaskbarLight` (Tasks 3, 4).
- Produces:
  ```cpp
  struct OverlayOptions { bool allTaskbars = false; bool showGraph = true; bool letters = false; UnitMode units = UnitMode::Bytes; };
  class OverlayManager { public:
    bool Init(HINSTANCE inst, HWND appWnd);
    void SetOptions(const OverlayOptions& o);
    void Update(std::optional<Speed> speed, const SpeedHistory& history);   // called every tick
    void SetTooltip(const std::wstring& text);
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp);                      // app window forwards; true if handled
    void Shutdown();                                                          // restores Win10 task list space
    std::function<void()> onLeftClick; std::function<void(POINT)> onRightClick; };
  ```

- [ ] **Step 1: Implement the lifecycle in `OverlayManager`.**
  - Keep one `OverlayWindow` per taskbar in use: primary only, or all of them when `allTaskbars` is set.
  - On `RegisterWindowMessage(L"TaskbarCreated")`, `WM_DISPLAYCHANGE` or `WM_DPICHANGED`: destroy all overlays, re-probe, recreate.
  - `SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, …, explorerPid, 0, WINEVENT_OUTOFCONTEXT)`, filtered to taskbar hwnds → reposition (this covers the auto-hide slide and taskbar resizes). Hide while `!IsTaskbarShown`.
  - `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` → per monitor, hide if `IsFullscreenOnMonitor(foreground)`. A window counts as shell if its class is `Progman`, `WorkerW` or a taskbar class. Hide while the foreground class is one of `Windows.UI.Core.CoreWindow`, `XamlExplorerHostIslandWindow`, `MultitaskingViewFrame`, `NotifyIconOverflowWindow` or `TopLevelWindowForOverflowXamlIsland`. Otherwise re-assert `HWND_TOPMOST` with `SWP_NOACTIVATE`.
  - `WM_SETTINGCHANGE` with `L"ImmersiveColorSet"` → re-read the theme and repaint.
  - Re-probe the UIA occupied extent every 2 s from the tick, but only while visible.
  - `SetTooltip`: one `TOOLTIPS_CLASS` control per overlay window (`TTF_SUBCLASS`, whole client rect), with its text updated in place.
- [ ] **Step 2: Win10 space reservation** (`taskbar_space_win10`, used only when `!IsWindows11()`). Shrink `MSTaskSwWClass` (child of `ReBarWindow32`) from its right or bottom end by the overlay width or height plus margin, so the task buttons never reach the overlay. Re-apply after any location change of `ReBarWindow32`, and restore the original size in `Shutdown()` and on `WM_ENDSESSION`. Probe `occupied` as empty when reservation is active.
- [ ] **Step 3: Manual verification matrix (Win10 here, and the Win11 VM for the Win11 rows), with each item recorded in the commit message:**
  - `taskkill /f /im explorer.exe & start explorer` → the overlay comes back within 2 s.
  - Turn on auto-hide → the overlay hides and reappears with the taskbar, and never floats alone.
  - Win10: move the taskbar to the top, left and right; it's placed correctly each time.
  - Two monitors: with `allTaskbars` off, it shows only on the primary; on, it shows on every taskbar.
  - Fullscreen video in a browser (F11 on YouTube) and a fullscreen game or `dxdiag`-style window → hidden, then back afterward.
  - Start menu, search, Action Center and tray overflow open → the overlay is never drawn over them.
  - Win10: open 30 windows → the task list scrolls rather than going under the overlay; exit the demo and the task list regains full width.
  - Measure CPU with `tools/measure.ps1` (written in Task 18; for now, Task Manager's Details tab over 60 s) → < 0.5%.
- [ ] **Step 4: Commit** `feat(ui): overlay lifecycle across explorer restarts, autohide, fullscreen, monitors`

---

### Task 6: Adapter enumeration, classification and counters

**Files:**
- Create: `src/platform/adapters.h/.cpp`, `tests/test_adapters.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct AdapterInfo { std::wstring id; /* interface GUID "{...}" from AdapterName */ NET_LUID luid; std::wstring name; /* FriendlyName */
                       std::wstring description; AdapterKind kind; bool up; uint64_t linkBitsPerSec; };
  bool IsListedInterface(ULONG ifType, TUNNEL_TYPE tunnelType);
  AdapterKind ClassifyAdapter(ULONG ifType, bool hardwareInterface);
  std::vector<AdapterInfo> EnumerateAdapters();
  std::optional<CounterReading> ReadCounters(NET_LUID luid, double nowSec);   // GetIfEntry2: InOctets, OutOctets, max(Receive,Transmit)LinkSpeed
  double MonotonicSeconds();                                                  // QueryPerformanceCounter
  ```

- [ ] **Step 1: Write the failing tests.**
  ```cpp
  TEST_CASE("IsListedInterface drops loopback and transition tunnels only") {
    CHECK(!IsListedInterface(IF_TYPE_SOFTWARE_LOOPBACK, TUNNEL_TYPE_NONE));
    CHECK(!IsListedInterface(IF_TYPE_TUNNEL, TUNNEL_TYPE_TEREDO));
    CHECK(!IsListedInterface(IF_TYPE_TUNNEL, TUNNEL_TYPE_ISATAP));
    CHECK(!IsListedInterface(IF_TYPE_TUNNEL, TUNNEL_TYPE_6TO4));
    CHECK(IsListedInterface(IF_TYPE_PROP_VIRTUAL, TUNNEL_TYPE_NONE));      // WireGuard
    CHECK(IsListedInterface(IF_TYPE_ETHERNET_CSMACD, TUNNEL_TYPE_NONE));
  }
  TEST_CASE("ClassifyAdapter") {
    CHECK(ClassifyAdapter(IF_TYPE_IEEE80211, true) == AdapterKind::WiFi);
    CHECK(ClassifyAdapter(IF_TYPE_IEEE80211, false) == AdapterKind::Virtual);   // Wi-Fi Direct virtual adapter
    CHECK(ClassifyAdapter(IF_TYPE_ETHERNET_CSMACD, true) == AdapterKind::Ethernet);
    CHECK(ClassifyAdapter(IF_TYPE_ETHERNET_CSMACD, false) == AdapterKind::Virtual); // Hyper-V vEthernet, TAP
    CHECK(ClassifyAdapter(IF_TYPE_WWANPP, true) == AdapterKind::Other);
  }
  TEST_CASE("EnumerateAdapters returns unique GUID ids without loopback" * doctest::test_suite("integration")) {
    auto list = EnumerateAdapters(); REQUIRE(!list.empty());
    std::set<std::wstring> ids; for (auto& a : list) { CHECK(a.id.front() == L'{'); CHECK(ids.insert(a.id).second); }
  }
  TEST_CASE("ReadCounters reads a live adapter" * doctest::test_suite("integration")) {
    auto list = EnumerateAdapters(); REQUIRE(!list.empty());
    CHECK(ReadCounters(list.front().luid, MonotonicSeconds()).has_value());
  }
  ```
- [ ] **Step 2: Run and confirm failure.**
- [ ] **Step 3: Implement.** `EnumerateAdapters` calls `GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER)`; that lists only adapters bound to TCP/IP, so lightweight-filter duplicates are excluded. Filter with `IsListedInterface`. Get `HardwareInterface` from `GetIfEntry2(...).InterfaceAndOperStatusFlags`. Set `up = OperStatus == IfOperStatusUp`. Retry the buffer size on `ERROR_BUFFER_OVERFLOW`.
- [ ] **Step 4: Run the tests** (unit and integration) → pass.
- [ ] **Step 5: Manual.** Print the list via a temporary test (`-s` flag). It must show Wi-Fi/Ethernet plus any vEthernet/VPN adapters, with one row each. Compare against `Get-NetAdapter -IncludeHidden` and confirm no "WFP"/"QoS Packet Scheduler" filter rows appear.
- [ ] **Step 6: Commit** `feat(platform): adapter enumeration, classification, counters`

---

### Task 7: Internet adapter detection and change notifications

**Files:**
- Create: `src/platform/internet_route.h/.cpp`, `tests/test_internet_route.cpp`

**Interfaces:**
- Produces:
  ```cpp
  std::optional<NET_LUID> FindInternetInterface();
  class NetChangeWatcher { public: bool Start(HWND notify, UINT msg); void Stop(); ~NetChangeWatcher(); };
  ```

- [ ] **Step 1: Write the failing integration test.**
  ```cpp
  TEST_CASE("FindInternetInterface matches an enumerated adapter when online" * doctest::test_suite("integration")) {
    auto luid = FindInternetInterface();
    if (!luid) { MESSAGE("offline; skipped"); return; }
    auto list = EnumerateAdapters();
    CHECK(std::any_of(list.begin(), list.end(), [&](auto& a) { return a.luid.Value == luid->Value; }));
  }
  ```
- [ ] **Step 2: Run and confirm failure.**
- [ ] **Step 3: Implement.** Call `GetBestInterfaceEx` for IPv4 `8.8.8.8`, falling back to IPv6 `2001:4860:4860::8888`, then `ConvertInterfaceIndexToLuid`. Add a code comment that this is a local routing-table lookup and sends no packets, and that it handles split-default VPN routes (`0.0.0.0/1` + `128.0.0.0/1`). `NetChangeWatcher` registers `NotifyRouteChange2(AF_UNSPEC)` and `NotifyIpInterfaceChange(AF_UNSPEC)`. Callbacks run on a threadpool thread, so `PostMessage(notify, msg)` only when an atomic "pending" flag goes false → true; the app clears it when it handles the message (coalescing).
- [ ] **Step 4: Run the tests** → pass.
- [ ] **Step 5: Commit** `feat(platform): internet interface detection and change watcher`

---

### Task 8: Network identity (Wi-Fi network names)

**Files:**
- Create: `src/platform/network_identity.h/.cpp`, `tests/test_network_identity.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct NetworkIdentity { std::wstring key; /* NLM network GUID "{...}" */ std::wstring name; /* may be empty */ };
  std::map<std::wstring /*adapter id*/, NetworkIdentity> QueryAdapterNetworks();   // caller has COM initialized (STA)
  ```

- [ ] **Step 1: Write the failing integration test.** If any adapter is up, the map is non-empty, every key starts with `{`, and every map key matches an `EnumerateAdapters()` id.
- [ ] **Step 2: Run and confirm failure.**
- [ ] **Step 3: Implement** using `INetworkListManager::GetNetworkConnections` → for each `INetworkConnection`: `GetAdapterId`, `GetNetwork` → `INetwork::GetNetworkId` / `GetName`. Format GUIDs with `StringFromGUID2`. The NLM name is the Wi-Fi SSID in practice. Don't use the WLAN API (spec §13.2: it's gated by location permission on newer Win11).
- [ ] **Step 4: Run the tests** → pass.
- [ ] **Step 5: Win11 check (VM, 24H2 or later)** with location services **off**: the Wi-Fi name is still returned. If it comes back empty, the app must show "Unknown Wi-Fi network" (Task 15) and must not prompt. Record the result in the commit message.
- [ ] **Step 6: Commit** `feat(platform): adapter-to-network identity via Network List Manager`

---

### Task 9: Sampler (per-tick deltas, internet series, display speed)

**Files:**
- Create: `src/core/sampler.h/.cpp`, `tests/test_sampler.cpp`

**Interfaces:**
- Consumes: `CounterTracker`, `CounterReading` (Task 2).
- Produces:
  ```cpp
  struct AdapterSample { std::wstring adapterId; CounterReading reading; };
  struct UsageDelta { std::wstring adapterId; uint64_t rx = 0, tx = 0; };
  struct TickResult { std::vector<UsageDelta> perAdapter; /* non-zero only */ std::optional<UsageDelta> internet; std::optional<Speed> display; };
  class Sampler { public:
    TickResult Tick(const std::vector<AdapterSample>& samples, const std::wstring& internetAdapterId, const std::wstring& displayAdapterId); };
  ```
  The app passes `displayAdapterId = internetAdapterId` in Automatic mode, and the pinned adapter's id in Manual mode.

- [ ] **Step 1: Write the failing tests.** `S(id, rx, tx, t)` builds a sample with a 1 Gbps link.
  ```cpp
  TEST_CASE("Sampler: per-adapter deltas and internet delta") {
    Sampler s; s.Tick({S("A",0,0,0), S("B",0,0,0)}, L"A", L"A");
    auto r = s.Tick({S("A",1000,100,1), S("B",500,0,1)}, L"A", L"A");
    CHECK(r.perAdapter.size() == 2); REQUIRE(r.internet); CHECK(r.internet->adapterId == L"A"); CHECK(r.internet->rx == 1000);
    REQUIRE(r.display); CHECK(r.display->rx == doctest::Approx(1000));
  }
  TEST_CASE("Sampler: internet series follows the internet adapter per tick") {
    Sampler s; s.Tick({S("A",0,0,0), S("B",0,0,0)}, L"A", L"A");
    auto r1 = s.Tick({S("A",1000,0,1), S("B",50,0,1)}, L"A", L"A");
    auto r2 = s.Tick({S("A",1000,0,2), S("B",2050,0,2)}, L"B", L"B");
    CHECK(r1.internet->adapterId == L"A"); CHECK(r1.internet->rx == 1000);
    CHECK(r2.internet->adapterId == L"B"); CHECK(r2.internet->rx == 2000);
  }
  TEST_CASE("Sampler: no internet adapter gives no display speed") {
    Sampler s; s.Tick({S("A",0,0,0)}, L"", L""); auto r = s.Tick({S("A",10,0,1)}, L"", L"");
    CHECK(!r.display); CHECK(!r.internet); CHECK(r.perAdapter.size() == 1);
  }
  TEST_CASE("Sampler: pinned adapter missing gives no display speed") {
    Sampler s; auto r = s.Tick({S("A",0,0,0)}, L"A", L"VPN"); CHECK(!r.display);
  }
  TEST_CASE("Sampler: invalid speed repeats last valid value, zero if none") {
    Sampler s; auto first = s.Tick({S("A",0,0,0)}, L"A", L"A");   // baseline: no valid speed yet
    REQUIRE(first.display); CHECK(first.display->rx == 0);
    s.Tick({S("A",4000,0,1)}, L"A", L"A");                         // 4000 B/s
    auto gap = s.Tick({S("A",8000,0,100)}, L"A", L"A");            // gap: speed invalid, usage counted
    REQUIRE(gap.display); CHECK(gap.display->rx == doctest::Approx(4000)); CHECK(gap.internet->rx == 4000);
  }
  TEST_CASE("Sampler: adapter that disappears and returns starts fresh") {
    Sampler s; s.Tick({S("A",0,0,0)}, L"A", L"A"); s.Tick({S("A",5000,0,1)}, L"A", L"A");
    s.Tick({}, L"", L"A");                                     // unplugged
    auto back = s.Tick({S("A",100,0,3)}, L"A", L"A");          // counters reset on replug
    CHECK(back.perAdapter.empty()); CHECK(!back.internet);     // baseline only
    auto next = s.Tick({S("A",1100,0,4)}, L"A", L"A"); CHECK(next.internet->rx == 1000);
  }
  ```
  `S(id, rx, tx, t)` takes a `const wchar_t*` id (e.g. `S(L"A", …)`); the short form above omits the `L` prefix for readability.
- [ ] **Step 2: Run and confirm failure.**
- [ ] **Step 3: Implement.** Keep `std::unordered_map<std::wstring, CounterTracker>` and `std::unordered_map<std::wstring, Speed> lastSpeed`. Erase trackers (and `lastSpeed`) for ids absent from `samples`.
- [ ] **Step 4: Run the tests** → pass.
- [ ] **Step 5: Commit** `feat(core): sampler producing usage deltas and display speed`

---

### Task 10: Time zones and report periods

**Files:**
- Create: `src/core/time_zone.h/.cpp`, `src/core/periods.h/.cpp`, `tests/test_periods.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct LocalDateTime { int year, month, day, hour, minute, second; };
  class TimeZone { public:
    static TimeZone Local();                                      // GetDynamicTimeZoneInformation
    static std::optional<TimeZone> FromKey(const std::wstring& key); // EnumDynamicTimeZoneInformation, e.g. L"Pacific Standard Time"
    LocalDateTime ToLocal(int64_t utcSec) const;
    int64_t ToUtc(const LocalDateTime& t) const; };               // nonexistent/ambiguous local times -> earliest valid instant
  enum class Period { Today, Yesterday, ThisWeek, LastWeek, ThisMonth, LastMonth, Last30Days, ThisYear, AllTime, Custom };
  UtcRange ResolvePeriod(Period p, int64_t nowUtc, const TimeZone& tz, int firstDayOfWeek /*0=Sun..6=Sat*/,
                         LocalDate customFrom = {}, LocalDate customTo = {});   // custom: inclusive dates
  int64_t HourStart(int64_t utcSec);
  LocalDate LocalDateOf(int64_t utcSec, const TimeZone& tz);
  int FirstDayOfWeekFromLocale();   // LOCALE_IFIRSTDAYOFWEEK (0=Mon) converted to 0=Sun
  ```

- [ ] **Step 1: Write the failing tests.** `UtcOf(y,mo,d,h)` uses `_mkgmtime`; `pst` is `*TimeZone::FromKey(L"Pacific Standard Time")`.
  ```cpp
  TEST_CASE("Today on DST fall-back day is 25 hours") {
    auto r = ResolvePeriod(Period::Today, UtcOf(2026,11,1,20), pst, 0);
    CHECK(r.begin == UtcOf(2026,11,1,7)); CHECK(r.end == UtcOf(2026,11,2,8));
  }
  TEST_CASE("Today on DST spring-forward day is 23 hours") {
    auto r = ResolvePeriod(Period::Today, UtcOf(2026,3,8,20), pst, 0);
    CHECK(r.begin == UtcOf(2026,3,8,8)); CHECK(r.end == UtcOf(2026,3,9,7));
  }
  TEST_CASE("ThisWeek honours first day of week") {   // 2026-10-18 is a Sunday
    CHECK(ResolvePeriod(Period::ThisWeek, UtcOf(2026,10,18,19), pst, 1).begin == UtcOf(2026,10,12,7));
    CHECK(ResolvePeriod(Period::ThisWeek, UtcOf(2026,10,18,19), pst, 0).begin == UtcOf(2026,10,18,7));
  }
  TEST_CASE("Last30Days is today plus 29 previous days") {
    auto r = ResolvePeriod(Period::Last30Days, UtcOf(2026,10,18,19), pst, 0);
    CHECK(r.begin == UtcOf(2026,9,19,7)); CHECK(r.end == UtcOf(2026,10,19,7));
  }
  TEST_CASE("Custom range is inclusive of both dates") {
    auto r = ResolvePeriod(Period::Custom, 0, pst, 0, {2026,1,1}, {2026,1,31});
    CHECK(r.begin == UtcOf(2026,1,1,8)); CHECK(r.end == UtcOf(2026,2,1,8));
  }
  TEST_CASE("HourStart floors to the hour") { CHECK(HourStart(UtcOf(2026,5,5,5) + 1799) == UtcOf(2026,5,5,5)); }
  ```
- [ ] **Step 2: Run and confirm failure.**
- [ ] **Step 3: Implement** with `SystemTimeToTzSpecificLocalTimeEx` and `TzSpecificLocalTimeToSystemTimeEx`. `AllTime` → `{0, start of tomorrow}`. `ThisYear` → Jan 1 to Jan 1 of next year. `LastWeek`/`LastMonth` are the full previous calendar unit.
- [ ] **Step 4: Run the tests** → pass.
- [ ] **Step 5: Commit** `feat(core): time zone conversion and report periods`

---

### Task 11: Usage accumulator and SQLite store

**Files:**
- Create: `third_party/sqlite/sqlite3.c`, `sqlite3.h`, `VERSION` (latest 3.x amalgamation from sqlite.org; record the version), `src/core/usage_accumulator.h/.cpp`, `src/storage/CMakeLists.txt`, `src/storage/usage_store.h/.cpp`, `tests/test_usage_accumulator.cpp`, `tests/test_usage_store.cpp`

**Interfaces:**
- Consumes: `UsageRow`, `DailyRow`, `Series`, `AdapterKind`, `LocalDate`, `UtcRange` (Task 1); `TimeZone`, `HourStart`, `LocalDateOf` (Task 10).
- Produces:
  ```cpp
  class UsageAccumulator { public:
    void Add(int64_t nowUtc, Series s, const std::wstring& adapterId, const std::wstring& networkKey, uint64_t rx, uint64_t tx);
    std::vector<UsageRow> Take(); bool Empty() const; };
  bool ShouldFlush(int64_t lastFlushUtc, int64_t nowUtc);       // >= 300 s elapsed or HourStart differs
  struct AdapterMeta { std::wstring id, name, friendlyName; AdapterKind kind; bool hidden = false; };
  struct NetworkMeta { std::wstring key, name; };
  class UsageStore { public:
    enum class OpenResult { Ok, RecoveredFromCorruption, Failed };
    OpenResult Open(const std::filesystem::path& file);
    std::filesystem::path RecoveredFile() const;                // path the corrupt db was moved to
    void AddUsage(std::span<const UsageRow> rows);              // adds to existing rows, one transaction
    void UpsertAdapter(const std::wstring& id, const std::wstring& name, AdapterKind kind);   // keeps friendlyName/hidden
    void UpsertNetwork(const NetworkMeta& n);
    void SetFriendlyName(const std::wstring& adapterId, const std::wstring& name);          // empty clears
    void SetHidden(const std::wstring& adapterId, bool hidden);
    std::vector<AdapterMeta> Adapters(); std::vector<NetworkMeta> Networks();
    std::vector<UsageRow> QueryHourly(UtcRange r);
    std::vector<DailyRow> QueryDaily(LocalDate from, LocalDate to);   // inclusive
    void RollUp(int64_t nowUtc, const TimeZone& tz);
    void ClearAll(); void ClearAdapter(const std::wstring& id); void ClearNetwork(const std::wstring& key); };
  ```

- [ ] **Step 1: Write the failing accumulator tests.** Two `Add`s in the same hour with the same key → one row with summed rx/tx. Adds at 10:59:59 and 11:00:00 → two rows with different `hourUtc`. `Take()` empties. `ShouldFlush(t, t+299)` is false, `ShouldFlush(t, t+300)` is true, and `ShouldFlush(UtcOf(..,10)+3599-60, UtcOf(..,11))` is true (hour changed).
- [ ] **Step 2: Write the failing store tests** (temp file via `std::filesystem::temp_directory_path()` plus a random name):
  - `AddUsage adds to existing rows`: the same row added twice → `QueryHourly` returns one row with doubled rx/tx.
  - `QueryHourly is half-open`: rows at `begin` and at `end` → only the `begin` row is returned.
  - `RollUp moves rows older than 90 days to local-day rows`: now = `UtcOf(2026,10,18,12)`, rows at `UtcOf(2026,7,1,6)` and `UtcOf(2026,7,1,8)` (both > 90 days old) plus one at `UtcOf(2026,10,10,0)`. After `RollUp(now, pst)`: `QueryDaily({2026,6,30},{2026,7,1})` has a `2026-06-30` row (06:00Z = June 30 PDT) and a `2026-07-01` row (08:00Z = July 1 01:00 PDT); `QueryHourly({0, now})` returns only the October row. Running `RollUp` a second time changes nothing.
  - `ClearAdapter removes hourly and daily rows for that adapter only`; `ClearNetwork` likewise; `ClearAll` empties both tables but keeps adapter metadata.
  - `UpsertAdapter preserves friendly name and hidden`.
  - `Open recovers from a corrupt file`: write 4 KB of `0xAB` bytes to the path → `Open` returns `RecoveredFromCorruption`, `RecoveredFile()` exists, and the store accepts `AddUsage`.
- [ ] **Step 3: Run and confirm failure.**
- [ ] **Step 4: Implement.** Schema (`meta.schema_version = '1'`):
  ```sql
  CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
  CREATE TABLE IF NOT EXISTS adapters(id TEXT PRIMARY KEY, name TEXT NOT NULL, kind INTEGER NOT NULL, friendly_name TEXT, hidden INTEGER NOT NULL DEFAULT 0);
  CREATE TABLE IF NOT EXISTS networks(key TEXT PRIMARY KEY, name TEXT NOT NULL);
  CREATE TABLE IF NOT EXISTS usage_hourly(hour_utc INTEGER NOT NULL, series INTEGER NOT NULL, adapter_id TEXT NOT NULL,
    network_key TEXT NOT NULL DEFAULT '', rx INTEGER NOT NULL, tx INTEGER NOT NULL, PRIMARY KEY(hour_utc, series, adapter_id, network_key)) WITHOUT ROWID;
  CREATE TABLE IF NOT EXISTS usage_daily(day TEXT NOT NULL, series INTEGER NOT NULL, adapter_id TEXT NOT NULL,
    network_key TEXT NOT NULL DEFAULT '', rx INTEGER NOT NULL, tx INTEGER NOT NULL, PRIMARY KEY(day, series, adapter_id, network_key)) WITHOUT ROWID;
  ```
  Use `PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;`, and write with `INSERT … ON CONFLICT DO UPDATE SET rx = rx + excluded.rx, tx = tx + excluded.tx`. `day` is `YYYY-MM-DD` local. The rollup cutoff is `HourStart(now) - 90*86400`, and it runs in one transaction. Detect corruption when open or `PRAGMA quick_check` fails or returns something other than `ok`; then close the db, rename it to `usage.corrupt-YYYYMMDD-HHMMSS.db` (and its `-wal`/`-shm` files), and create a fresh one. Strings are stored as UTF-8. Compile SQLite with `SQLITE_OMIT_LOAD_EXTENSION` and `SQLITE_DQS=0`.
- [ ] **Step 5: Run the tests** → pass.
- [ ] **Step 6: Commit** `feat(storage): usage accumulator and SQLite usage store with rollup`

---

### Task 12: Settings, data folder and autostart

**Files:**
- Create: `src/platform/settings.h/.cpp`, `src/platform/paths.h/.cpp`, `src/platform/autostart.h/.cpp`, `tests/test_settings.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct Settings { bool startWithWindows = true; std::wstring adapterId; /* empty = Automatic */ UnitMode units = UnitMode::Bytes;
                    bool showGraph = true; bool letters = false; bool allTaskbars = false; std::optional<RECT> reportRect; };
  Settings LoadSettings(const std::filesystem::path& ini);
  bool SaveSettings(const std::filesystem::path& ini, const Settings& s);
  std::filesystem::path DataDir();                                   // %LOCALAPPDATA%\NetworkSpeedMonitor, created if missing
  bool SetAutostart(bool enable, const std::wstring& valueName = L"NetworkSpeedMonitor");   // HKCU\...\CurrentVersion\Run = "\"<exe path>\""
  bool IsAutostartEnabled(const std::wstring& valueName = L"NetworkSpeedMonitor");
  ```
- [ ] **Step 1: Write the failing tests.**
  - A round trip of non-default values is equal.
  - A missing file → defaults.
  - A file with `Units=furlongs`, `ShowGraph=maybe` → those keys are defaults; the others are still parsed.
  - Integration test: `SetAutostart(true, L"NetworkSpeedMonitorTest")` → `IsAutostartEnabled` is true → `SetAutostart(false, …)` → false.
- [ ] **Step 2: Run and confirm failure.**
- [ ] **Step 3: Implement** with `GetPrivateProfileStringW`/`WritePrivateProfileStringW`. Section `[General]` holds keys `StartWithWindows`, `Adapter`, `Units` (`bytes`|`bits`), `ShowGraph`, `Letters`, `AllTaskbars`; section `[ReportWindow]` holds `Left`, `Top`, `Right`, `Bottom`. Booleans are `0`/`1`. Get `DataDir` from `SHGetKnownFolderPath(FOLDERID_LocalAppData)`.
- [ ] **Step 4: Run the tests** → pass.
- [ ] **Step 5: Commit** `feat(platform): settings, data folder, autostart`

---

### Task 13: App shell: real data on the taskbar, recording, menu, power

**Files:**
- Create: `src/ui/app.h/.cpp`
- Modify: `src/ui/main.cpp` (`wWinMain` → `App().Run(...)`; keep `--demo`), `res/strings.rc`, `res/resource.h`

**Interfaces:**
- Consumes: everything from Tasks 2–12.
- Produces:
  ```cpp
  class App { public: int Run(HINSTANCE inst, const std::wstring& cmdLine);
    void ShowReport(int tab = 0);   // stub here; implemented by Task 15
    void FlushNow(); };             // write accumulator to store
  ```
  Constants: mutex `L"Local\\NetworkSpeedMonitor.SingleInstance.7d3c6a52-0c8e-4f0f-9b1e-5a7f2c1d9e43"`, message-window class `L"NetworkSpeedMonitor.AppWindow"`, `WM_APP_SHOWREPORT = WM_APP + 1`, `WM_APP_NETCHANGED = WM_APP + 2`.

- [ ] **Step 1: App window and single instance.** The app window is a hidden **top-level** `WS_POPUP` window that is never shown. It must not be `HWND_MESSAGE`, because message-only windows don't receive the `TaskbarCreated` and `WM_SETTINGCHANGE` broadcasts. If the mutex already exists, `FindWindow(L"NetworkSpeedMonitor.AppWindow")` → post `WM_APP_SHOWREPORT` and exit 0.
- [ ] **Step 2: Startup.** Call `SetProcessDpiAwarenessContext` (or rely on the manifest), `CoInitializeEx(COINIT_APARTMENTTHREADED)`, `DataDir`, `LoadSettings`, `SetAutostart(settings.startWithWindows)`, `UsageStore::Open(DataDir()/L"usage.db")`. If the result is `RecoveredFromCorruption`, remember it for the report banner; if `Failed`, keep running without recording. Then `RollUp`, `OverlayManager::Init`, `NetChangeWatcher::Start`, an initial network refresh, and `SetTimer` at 1000 ms.
- [ ] **Step 3: Network refresh** (startup and on `WM_APP_NETCHANGED`): `EnumerateAdapters`, `FindInternetInterface` → map the LUID to an adapter id, `QueryAdapterNetworks`, `UpsertAdapter`/`UpsertNetwork`, and update the tooltip. Tooltip copy: Automatic → `IDS_TIP_AUTO` "Automatic: %s"; pinned → the adapter's display name. A display name is the friendly name if set, else `AdapterInfo::name`.
- [ ] **Step 4: Tick.** `ReadCounters` for each known adapter → `Sampler::Tick`. For each `perAdapter` delta: `Accumulator.Add(now, Series::Adapter, id, networkKeyOf(id), …)`. For `internet`: `Add(now, Series::Internet, id, networkKeyOf(id), …)`. Then push `display` (or `{0,0}` when nullopt) to `SpeedHistory`, call `OverlayManager::Update(display, history)`, and `FlushNow()` if `ShouldFlush`. Run `RollUp` once every 24 h of uptime.
- [ ] **Step 5: Power and exit.** Call `FlushNow()` on `WM_POWERBROADCAST/PBT_APMSUSPEND`, `WM_QUERYENDSESSION`, `WM_ENDSESSION` and Exit. On Exit, also `OverlayManager::Shutdown()` and `SaveSettings`.
- [ ] **Step 6: Right-click menu** (§6.5). The Adapter submenu lists `IDS_MENU_AUTOMATIC` "Automatic" first, then non-hidden adapters by display name; the current choice is checked. Units submenu: "Bits"/"Bytes" (radio). Then "Show graph" (check), "Use letters (U/D)" (check), separator, "Open report…", "Settings…", separator, "Exit". Changes apply immediately and are saved. Left-click → `ShowReport(0)`. All copy goes in `strings.rc`.
- [ ] **Step 7: Manual verification.**
  - Start a large download. The overlay's ↓ matches Task Manager's adapter graph within about 5% over 60 s.
  - Unplug Ethernet or turn Wi-Fi off → in Automatic mode it switches within 2 s and the tooltip updates.
  - Pin the VPN, then disconnect it → shows `—`.
  - Sleep for 2 min and resume → no spike.
  - `usage.db` mtime changes only about every 5 min (`Get-Item $env:LOCALAPPDATA\NetworkSpeedMonitor\usage.db | % LastWriteTime`).
  - A second launch does not create a second overlay.
- [ ] **Step 8: Commit** `feat(app): live sampling, recording, context menu, power handling`

---

### Task 14: Report model and CSV export

**Files:**
- Create: `src/report/CMakeLists.txt`, `src/report/report_model.h/.cpp`, `src/report/csv_export.h/.cpp`, `tests/test_report_model.cpp`, `tests/test_csv_export.cpp`

**Interfaces:**
- Consumes: `UsageStore`, `AdapterMeta`, `NetworkMeta` (Task 11); `TimeZone`, `ResolvePeriod`, `LocalDateOf` (Task 10).
- Produces:
  ```cpp
  struct Totals { uint64_t rx = 0, tx = 0; uint64_t Total() const { return rx + tx; } };
  struct NamedTotals { std::wstring id; std::wstring name; /* empty name = unknown network */ AdapterKind kind; bool hidden = false; Totals totals; };
  enum class Granularity { Hour, Day, Month };
  struct TimelineBar { LocalDateTime start; Totals totals; };
  struct SummaryData { Totals today, thisWeek, thisMonth, period; std::optional<std::pair<LocalDate, Totals>> busiestDay;
                       std::vector<NamedTotals> topAdapters, topNetworks; /* top 3 in period */ };
  class ReportModel { public:
    ReportModel(UsageStore& store, const TimeZone& tz, int firstDayOfWeek);
    SummaryData Summary(UtcRange period, int64_t nowUtc);
    Granularity PickGranularity(UtcRange r, int64_t nowUtc);
    std::vector<TimelineBar> Timeline(UtcRange r, int64_t nowUtc);
    std::vector<NamedTotals> AdapterTable(UtcRange r, bool includeHidden);
    std::vector<NamedTotals> WifiTable(UtcRange r); };
  std::string CsvEscape(std::string_view utf8);
  bool ExportCsv(const std::filesystem::path& file, UsageStore& store, UtcRange r, const TimeZone& tz);
  ```
- [ ] **Step 1: Write the failing tests** (in-memory temp store, `pst`):
  - `Summary uses internet series only`: the adapter series has VPN 1 GB and Wi-Fi 1.05 GB today; the internet series has VPN 1 GB → `today.Total() == 1'000'000'000`.
  - `Tables use raw adapter series`: `AdapterTable` returns both the VPN (1 GB) and the Wi-Fi (1.05 GB), sorted by total descending.
  - `Busiest day picks the largest internet day in the period`.
  - `Friendly name replaces adapter name; hidden excluded unless requested`.
  - `WifiTable groups Wi-Fi adapter rows by network and keeps unknown`: rows with `networkKey` `{N1}` and `""` on a Wi-Fi adapter → two rows, the unknown one with an empty `name`; Ethernet rows are excluded.
  - `PickGranularity`: Today → Hour; a day 100 days ago → Day; Last30Days → Day; ThisYear → Month.
  - `Timeline on fall-back day has 25 hourly bars`.
  - `Timeline mixes daily and hourly rows`: ThisYear with a daily row in March and hourly rows in October → March and October bars carry the right totals.
  - CSV: `CsvEscape quotes commas and quotes`: `CsvEscape(R"(Wi-Fi "Home", 5G)") == R"("Wi-Fi ""Home"", 5G")"`; `CsvEscape("plain") == "plain"`.
  - `ExportCsv writes UTF-8 with BOM`: the file starts with `EF BB BF`, then exactly `start_local,granularity,series,adapter_id,adapter_name,adapter_kind,network,received_bytes,sent_bytes\r\n`; an adapter named `Café` round-trips as UTF-8.
- [ ] **Step 2: Run and confirm failure.**
- [ ] **Step 3: Implement.** Headline numbers (`today`, `thisWeek`, `thisMonth`, `period`, `busiestDay`, timeline) use `Series::Internet`; tables use `Series::Adapter`. Combine `QueryHourly(r)` with `QueryDaily(LocalDateOf(r.begin), LocalDateOf(r.end - 1))`. Granularity: ≤ 25 h and `r.begin >= HourStart(now) - 90*86400` → Hour; ≤ 92 days → Day; otherwise Month. CSV: one row per stored row in range (hourly rows `YYYY-MM-DD HH:00` / `hour`, daily `YYYY-MM-DD` / `day`); series `adapter`|`internet`; kind `wifi|ethernet|virtual|other`; network = network name or empty; CRLF line endings.
- [ ] **Step 4: Run the tests** → pass.
- [ ] **Step 5: Commit** `feat(report): report model and CSV export`

---

### Task 15: Report window

**Files:**
- Create: `src/ui/report_window.h/.cpp`, `src/ui/report_paint.h/.cpp`, `src/ui/dialogs.h/.cpp` (rename, custom range, confirm)
- Modify: `src/ui/app.cpp` (`ShowReport`, flush before refresh), `res/strings.rc`, `res/app.rc` (dialog templates)

**Interfaces:**
- Consumes: `ReportModel`, `ExportCsv` (Task 14); `UsageStore` (Task 11); `ResolvePeriod` (Task 10); `FormatVolume` (Task 1); theme functions (Task 4).
- Produces:
  ```cpp
  class ReportWindow { public:
    void Show(int tab);                 // create or bring to front; tab 0..3 = Summary, Timeline, Adapters, Wi-Fi networks
    void Refresh(); bool IsOpen() const;
    std::function<void()> beforeQuery;  // App::FlushNow
    std::function<void(RECT)> onBoundsChanged;
    void SetCorruptionNotice(std::filesystem::path recoveredFile); };
  ```
- [ ] **Step 1: Window.** A resizable top-level window that restores `Settings::reportRect` (clamped to a visible monitor). Closing destroys it (to keep idle memory low) and the app keeps running. Top row: Period combobox (the §9.1 list; "Custom range…" opens a two-date dialog), "Export CSV…" button, "Clear all history…" button. Below that, a `WC_TABCONTROL` with tabs Summary, Timeline, Adapters, Wi-Fi networks. Refresh on open, on period change, and every 60 s while visible, calling `beforeQuery` first.
- [ ] **Step 2: Summary and Timeline painting** (`report_paint`, Direct2D). Summary shows cards for Today, This week, This month (`FormatVolume` total, with `↓ x · ↑ y` beneath), "Busiest day", "Top adapters", "Top Wi-Fi networks", and the note `IDS_NOTE_COVERAGE` "Usage is recorded only while Network Speed Monitor is running." If a corruption notice is set, show a banner: `IDS_NOTE_CORRUPT` "Your usage history couldn't be read, so a new one was started. The old file was kept as %s." Timeline is a stacked bar chart: received in the download color, sent in the upload color (same hues as the overlay). Axis labels are hours, days or months per `PickGranularity`; hovering a bar shows exact values in a tooltip.
- [ ] **Step 3: Tables.** The Adapters tab is a ListView (report mode) with columns Name, Type, Received, Sent, Total. Clicking a column sorts it (toggling direction), and there's a "Show hidden adapters" checkbox. Right-click a row: "Rename…", "Hide"/"Unhide", "Clear this adapter's history…". The Wi-Fi networks tab has columns Name, Received, Sent, Total; an empty name shows `IDS_UNKNOWN_WIFI` "Unknown Wi-Fi network"; right-click: "Clear this network's history…". Both tabs show `IDS_NOTE_RAW` "Each row is that adapter's or network's own traffic, so rows can add up to more than your internet total (a VPN and the Wi-Fi it runs over both count)."
- [ ] **Step 4: Actions.**
  - Export uses `IFileSaveDialog` with default name `network-usage-YYYY-MM-DD.csv` and exports the selected period across all tabs.
  - Clear confirmations use `TaskDialogIndirect` with a "Delete"/"Cancel" pair (default Cancel). Copy: `IDS_CONFIRM_CLEAR_ALL` "Delete all usage history? This can't be undone." / `IDS_CONFIRM_CLEAR_ONE` "Delete all usage history for “%s”? This can't be undone."
  - Rename is a dialog with one edit box; an empty value restores the original name.
- [ ] **Step 5: Look and accessibility.** When `!AreAppsLight()`, set `DWMWA_USE_IMMERSIVE_DARK_MODE`, paint custom surfaces dark, and apply `SetWindowTheme(listview, L"DarkMode_Explorer", nullptr)`. When `IsHighContrast()`, paint only with `GetSysColor` colors. Keyboard: Tab order through all controls, Ctrl+Tab/Ctrl+Shift+Tab switch tabs, Esc closes.
- [ ] **Step 6: Manual verification.**
  - Open from the overlay.
  - Each period updates all tabs.
  - Totals on Summary equal the Timeline sum for the same period.
  - Rename shows up in the right-click menu immediately.
  - Hide removes the adapter from the menu.
  - Clearing a network removes only its rows.
  - The CSV opens in Excel with correct columns.
  - Dark mode, light mode and High Contrast are all readable.
  - Closing the window drops private memory back under 15 MB within a minute.
- [ ] **Step 7: Commit** `feat(ui): tabbed usage report window`

---

### Task 16: Settings dialog and About

**Files:**
- Modify: `src/ui/dialogs.h/.cpp`, `src/ui/app.cpp`, `res/app.rc`, `res/strings.rc`, `CMakeLists.txt` (pass `NSM_SOURCE_URL` and the version as compile definitions)

**Interfaces:**
- Consumes: `Settings`, `SaveSettings`, `SetAutostart`, `DataDir` (Task 12); `EnumerateAdapters` (Task 6); `ReportWindow::Show` (Task 15).
- Produces: `bool ShowSettingsDialog(HWND owner, Settings& s, const std::vector<std::pair<std::wstring,std::wstring>>& adapters /*id, display name*/);` (true = OK)

- [ ] **Step 1: Implement** a modal dialog (resource `IDD_SETTINGS`) with these controls:
  - "Start with Windows" checkbox
  - "Adapter" combobox (Automatic + adapters)
  - "Units" radio buttons (Bytes / Bits)
  - "Show mini graph" and "Use letters (U/D)" checkboxes
  - "Show on" radio buttons (Primary taskbar only / All taskbars)
  - "Manage adapters…" button (opens the report on the Adapters tab)
  - "Open data folder" button (`ShellExecuteW(L"open", DataDir())`)
  - About line: "Network Speed Monitor <version> · MIT License". Add a SysLink to `NSM_SOURCE_URL` only when it's non-empty.

  OK applies everything immediately (overlay options, autostart, `SaveSettings`); Cancel discards.
- [ ] **Step 2: Manual verification.** Each control changes behavior without a restart, settings survive a restart, and unchecking "Start with Windows" removes the `Run` value.
- [ ] **Step 3: Commit** `feat(ui): settings dialog and about`

---

### Task 17: Installer, winget, release pipeline, repository docs

**Files:**
- Create: `installer/NetworkSpeedMonitor.iss`, `packaging/winget/README.md` plus manifest templates (`*.yaml`), `.github/workflows/release.yml`, `README.md`, `PRIVACY.md`, `CONTRIBUTING.md`, `SECURITY.md`, `THIRD_PARTY_NOTICES.md`, `docs/signing.md`

- [ ] **Step 1: Inno Setup 6 script.**
  - Setup: `PrivilegesRequired=lowest`, `DefaultDirName={localappdata}\Programs\NetworkSpeedMonitor`, a fixed `AppId` GUID (generate once with `[guid]::NewGuid()` and never change it), and `AppMutex=Local\NetworkSpeedMonitor.SingleInstance.7d3c6a52-0c8e-4f0f-9b1e-5a7f2c1d9e43`.
  - Shortcuts and launch: a Start menu shortcut, plus "Launch now" on the finish page.
  - Uninstall: closes the running app (`CloseApplications=yes`) and deletes the `HKCU\…\Run\NetworkSpeedMonitor` value (`[Registry]` with `uninsdeletevalue`).
  - It asks before deleting history only when the uninstall is interactive:
  ```pascal
  procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
  begin
    if (CurUninstallStep = usPostUninstall) and (not UninstallSilent) then
      if MsgBox('Also delete your usage history?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(ExpandConstant('{localappdata}\NetworkSpeedMonitor'), True, True, True);
  end;
  ```
- [ ] **Step 2: Release workflow** on tag `v*.*.*`:
  - Build x64 and ARM64 Release with `-DNSM_SOURCE_URL=https://github.com/${{ github.repository }}`, and run the x64 tests.
  - Run `tools/check_imports.ps1` (Task 18).
  - Install Inno Setup (`choco install innosetup -y`) and compile one installer per architecture.
  - Write `SHA256SUMS.txt`, then create the GitHub Release with the installers and the checksums.
  - Leave a clearly marked, disabled SignPath signing step and document how to enable it in `docs/signing.md` (apply to SignPath Foundation, add the secrets, flip `if:`).
- [ ] **Step 3: winget.** Add templates for `NetworkSpeedMonitor.NetworkSpeedMonitor` (version, installer, locale manifests; `InstallerType: inno`, `Scope: user`, x64 + arm64), and a README explaining how to submit each release to `microsoft/winget-pkgs` (e.g. with `wingetcreate update`).
- [ ] **Step 4: Docs.**
  - `README.md`: what it is, a screenshot, install (winget / installer), the privacy promise, how to build.
  - `PRIVACY.md`: no network connections, no telemetry, data stays in `%LOCALAPPDATA%\NetworkSpeedMonitor`, how to delete it.
  - `CONTRIBUTING.md`: build, tests, strings go in `strings.rc`, the no-network rule.
  - `SECURITY.md`: report via GitHub private advisories.
  - `THIRD_PARTY_NOTICES.md`: SQLite (public domain, version), doctest (MIT, tests only).
- [ ] **Step 5: Verification** (Win10 here, Win11 VM).
  - Install as a standard user → no UAC prompt; the app starts and autostarts after logoff/logon.
  - `winget validate packaging\winget\<version dir>` passes.
  - An interactive uninstall asks about history; `"unins000.exe" /VERYSILENT` keeps `%LOCALAPPDATA%\NetworkSpeedMonitor`.
- [ ] **Step 6: Commit** `build: installer, winget manifests, release pipeline, docs`

---

### Task 18: Final verification against the anti-goals

**Files:**
- Create: `tools/check_imports.ps1`, `tools/measure.ps1`
- Modify: `.github/workflows/ci.yml` (run `check_imports.ps1`)

- [ ] **Step 1: `tools/check_imports.ps1 <exe>`** runs `dumpbin /imports` (located via vswhere) and fails if any of `ws2_32.dll`, `winhttp.dll`, `wininet.dll` or `urlmon.dll` is imported. Add it to CI after the Release build. Run it locally → passes.
- [ ] **Step 2: `tools/measure.ps1 -Minutes 10`** samples `Get-Process NetworkSpeedMonitor` every 5 s and prints average CPU % (Δ TotalProcessorTime / wall time / logical cores), max `PrivateMemorySize64`, and the `usage.db` write count (LastWriteTime changes). Expected with only the overlay showing: CPU < 0.5%, private memory < 15 MB, ≤ 2 db writes per 10 min.
- [ ] **Step 3: Network silence.** While running, `Get-NetTCPConnection -OwningProcess (Get-Process NetworkSpeedMonitor).Id` and `Get-NetUDPEndpoint -OwningProcess …` both return nothing.
- [ ] **Step 4: Re-run the Task 5 and Task 13 manual matrices** on the Release build on Win10 and the Win11 VM. Record the results in `docs/verification-v0.1.0.md`.
- [ ] **Step 5: Commit** `test: import check, resource measurement, v0.1.0 verification record`
