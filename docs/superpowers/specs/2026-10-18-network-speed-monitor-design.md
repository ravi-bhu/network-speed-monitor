# Network Speed Monitor: Design

Date: 2026-10-18
Status: Draft, awaiting approval
License: MIT

## 1. Intent and why

A small, native Windows tool that shows live upload and download speed directly on the taskbar, and records how much data you use over time.

Tools like NetSpeedMonitor, TrafficMonitor and NetSpeedTray already exist. This project exists to be the one people can **trust**: open source under MIT, no ads, no telemetry, no bundled extras, no network connections of its own, and light enough that you forget it's running.

Success is a tool that people install once and never think about. It sits quietly on the taskbar showing correct numbers, and when you click it, it tells you how much data you used, where and when.

## 2. Goals

1. A live up/down speed readout drawn **on the taskbar** (not just a tray icon), on Windows 10 and Windows 11.
2. Monitors any adapter Windows reports: Wi-Fi, Ethernet and virtual (VPN, Hyper-V/WSL/Docker vEthernet, VirtualBox/VMware and so on). Defaults to automatically following the adapter that carries internet traffic, with a manual override.
3. User-selectable units: bits or bytes, auto-scaled.
4. A report window, opened by clicking the readout, that answers **"how much data did I use?"** by time period, adapter, Wi-Fi network, and upload vs. download.
5. Usage history kept indefinitely, at a small, bounded size on disk.
6. Distributed as a signed installer and via winget.

## 3. Non-goals (version 1)

- Per-application bandwidth breakdown.
- Speed-over-time history in the report. The report is about data volume; the only speed history is the in-memory mini graph on the taskbar.
- Adapter detail views (IP addresses, signal strength, link speed and so on).
- Data caps, billing cycles, limit alerts or notifications.
- A background Windows service, or counting traffic before login or while the app isn't running.
- Portable/zip distribution and Microsoft Store.
- Update checks of any kind (users update via winget or by downloading a new installer).
- Windows 7/8/8.1.
- Full theming (custom fonts, colors, sizes).
- Languages other than English at launch.

## 4. Anti-goals (failure even if it works)

These are hard requirements, not nice-to-haves.

- **Noticeable resource use.** Target under ~0.5% CPU averaged at idle and under ~15 MB private working set while only the overlay is showing. No constant disk activity: database writes are batched (see §8.3).
- **Overlay glitches.** No flicker. Never overlaps tray icons or taskbar buttons. Survives Explorer restarts and taskbar changes. Never draws over the Start menu, the taskbar flyouts, or fullscreen apps.
- **Wrong numbers.** Speeds must agree with Task Manager for the same adapter. Totals must not double count, miss traffic while running, or spike after sleep, resume, adapter reset or reboot.
- **Admin rights or nagging.** Runs and installs entirely as a standard user. No popups, no "rate us", no permission requests beyond what a feature strictly needs.
- **Any outbound network traffic from the app itself.** Zero, including no telemetry, crash reporting or update pings.

## 5. Constraints

- **Language and UI stack:** C++ with raw Win32 (plus GDI/Direct2D/DirectWrite for drawing). One small exe, no runtime to install. No WebView, .NET, Qt or Electron.
- **OS:** Windows 10 and Windows 11, both x64. (ARM64 is the builder's call; see §11.)
- **Privileges:** standard user only, both for running and for installing.
- **License:** MIT. Any bundled third-party code must be MIT-compatible (e.g. SQLite, which is public domain).
- **Privacy:** the app makes no network connections. This should be easy to verify from the source.

## 6. The taskbar readout (decided)

### 6.1 Appearance

Layout "C" from the brainstorm: two stacked lines, with a mini live graph to the left.

```
 [~~mini graph~~]  ↑ 1.24 MB/s
                   ↓ 15.8 MB/s      [tray icons] [clock]
```

- Top line is upload (↑), bottom line is download (↓). Numbers use tabular figures so the text doesn't jitter.
- The mini graph shows roughly the last 60 seconds of upload and download as two lines in two colors. It's in memory only and never persisted.
- Colors follow the Windows taskbar light/dark setting automatically, and update live when the user switches theme.
- The readout sits just left of the notification area (tray). On a vertical taskbar (Win10), the builder picks a sensible compact arrangement that fits the taskbar width.
- It's DPI-aware per monitor, and looks crisp at 100–200% scaling and when moved between monitors with different DPI.

### 6.2 User toggles (only these)

- Show/hide the mini graph.
- Arrows (↑ ↓) vs. letter labels (U: / D:).

### 6.3 Update rate

Once per second. Not configurable.

### 6.4 Taskbar setups that must work

- **Multiple monitors:** shows on the primary monitor's taskbar by default. A setting allows showing it on every taskbar.
- **Auto-hide taskbar:** the readout hides and shows together with the taskbar; it never floats alone.
- **Taskbar moved to the top, left or right** (Windows 10; Windows 11 is bottom-only).
- **Fullscreen apps:** the readout hides while a fullscreen app (game, video) is in front and comes back afterward.
- **Explorer restart / taskbar recreated:** the readout reattaches automatically (listen for the `TaskbarCreated` broadcast message).
- **Taskbar layout changes:** tray grows or shrinks, task buttons fill up, Win11 taskbar alignment is left or center, taskbar size changes. The readout repositions and never overlaps.

### 6.5 Interaction

- **Left-click** the readout to open the report window (or bring it to the front if it's already open).
- **Right-click** the readout for a menu:
  - Adapter: **Automatic** (default) plus a list of every adapter, showing its friendly name if the user renamed it. A check mark shows the active choice.
  - Units: Bits / Bytes.
  - Show graph (toggle), Arrows / Letters.
  - Open report…, Settings…, Exit.
- There is **no separate tray icon.** The overlay is the only UI entry point.
- Hovering shows a tooltip with the adapter currently being measured (useful in Automatic mode).

## 7. Measuring speed (decided)

### 7.1 Which adapters exist

List every network interface Windows reports, physical and virtual, **except** pure plumbing that would only confuse users: loopback, and the per-adapter filter/QoS/WFP "lightweight filter" interfaces Windows exposes as duplicates of real adapters. The exact filtering rule is the builder's call, but the outcome must be one entry per real or virtual adapter a user would recognize.

Identify adapters by a stable ID that survives reboots (the interface GUID), never by list position or display name.

### 7.2 Automatic mode

The default. It measures the adapter that currently carries the system's default internet route (the best default route). It switches automatically when that changes (Wi-Fi ↔ Ethernet, VPN connects or disconnects), reacting to route and interface change notifications rather than polling.

On a full-tunnel VPN, that means it measures the VPN adapter. That's intended: it shows what is flowing to and from the internet, without adding the VPN and the physical adapter together.

### 7.3 Manual mode

The user picks any single adapter from the list. If that adapter disappears (unplugged, VPN disconnected), the readout shows a dash (`—`) for both values until it comes back. It does **not** silently fall back to Automatic.

### 7.4 Calculation

- Read each adapter's 64-bit cumulative received/sent byte counters once per second.
- Speed = Δbytes ÷ actual elapsed time, measured with a monotonic high-resolution clock, not the timer's nominal 1 s.
- **Counter resets and gaps (live speed):** if a counter goes backwards (reboot, adapter reset, driver reload), or the elapsed time is far longer than expected (sleep/resume, the process being suspended), discard that sample for the speed readout instead of producing a spike. Usage recording treats gaps differently; see §8.1. Rule of thumb for the builder: never report a rate or count a delta that exceeds the adapter's reported link speed for the elapsed time.
- Speeds should match Task Manager's numbers for the same adapter, within normal sampling jitter.

### 7.5 Units

- User chooses **bits** (bps, Kbps, Mbps, Gbps) or **bytes** (B/s, KB/s, MB/s, GB/s). Default: **bytes**. *(My call.)*
- Auto-scaled: the unit steps up or down as the speed changes, keeping about 3 significant figures (e.g. `950 KB/s`, `1.24 MB/s`, `15.8 MB/s`).
- Decimal (1 KB = 1000 B) for both bits and bytes, matching how ISPs and Task Manager express speeds. *(My call.)*
- The unit setting applies to the taskbar speed. Data **volumes** in the report are always bytes (MB, GB, TB), auto-scaled, decimal. *(My call: "gigabits used" isn't a thing people think in.)*

## 8. Recording usage (decided)

### 8.1 What's recorded

All recording happens while the app is running, independently of which adapter the overlay is showing. Every adapter's traffic is recorded all the time, so the report can break down by adapter regardless of the overlay setting.

For each hour, it records bytes received and bytes sent, keyed by:
- the **adapter** (stable ID, plus its name and type at the time), and
- the **network** the adapter was connected to during that hour, where one is identifiable. For Wi-Fi that's the network name (SSID). For other adapters, it's whatever network identity Windows provides, or none.

Alongside the per-adapter numbers, it records an **internet usage** series: for each sample, the delta of whichever adapter carried the default internet route at that moment (the same rule as Automatic mode, §7.2). That series drives the headline totals in the report (§9.1), so VPN-over-Wi-Fi or WSL/Docker/Hyper-V traffic is never counted twice. *(Your decision.)* How it's stored (a separate series, or a per-row "was the internet adapter" flag) is the builder's call.

Each sample's deltas use the same reset protection as §7.4, with one difference for usage:

- **Gaps count.** If the app was suspended or the machine was in sleep or Modern Standby, and the counters moved by a plausible amount (within link speed × elapsed time, and the counter didn't go backwards), **count that delta toward usage**. Laptops keep transferring data in Modern Standby, and the totals should include it. *(Your decision.)* Attribute the delta to the hour in which it was observed. Spreading it across the gap is the builder's call.
- The **live speed readout** still discards that sample, so the taskbar never shows a spike after resume.
- If the counter went backwards (reboot, adapter reset), the bytes before the reset since the last sample are unknowable. Drop that delta and resume from the new baseline.

### 8.2 Coverage

- The app starts automatically at login. This is on by default and can be turned off in Settings. It uses the per-user `Run` registry key, with no scheduled task or service.
- Traffic before login, or while the app isn't running, **is not counted**. The report states this plainly (e.g. a small note on the Summary tab: "Usage is recorded only while Network Speed Monitor is running.").
- The app does not try to back-fill gaps from Windows' own counters.

### 8.3 Storage

- A single SQLite database in `%LOCALAPPDATA%\NetworkSpeedMonitor\`, with SQLite compiled into the exe. *(My call, confirmed by you in playback.)*
- Settings live in the same folder (the format is the builder's call; a small INI or JSON file, or a settings table in the DB).
- **Write batching:** keep the current hour's counters in memory and write to the database every few minutes (builder's call, roughly 5 min), on the hour boundary, and immediately on sleep, shutdown, logoff and exit. The disk must not be touched every second.
- **Retention:** hourly rows are kept for 90 days, then rolled up into daily rows that are kept forever. Rollup runs at startup and once a day.
- Timestamps are stored in UTC and shown in local time. Days and weeks follow the user's local time zone; the first day of the week follows the Windows regional setting. *(My call.)*
- If the database is corrupt or unreadable, the app keeps working (overlay unaffected). It moves the bad file aside, starts a fresh one, and says so in the report window. It never crashes or nags.

## 9. Report window (decided)

A normal resizable top-level window that remembers its size and position. Opened from the overlay. Closing it hides it; the app keeps running.

### 9.1 Layout

Tabbed (layout "B" from the brainstorm). A **Period** picker sits at the top and applies to all tabs: Today, Yesterday, This week, Last week, This month, Last month, Last 30 days, This year, All time, Custom range…

**What the numbers mean:** the Summary cards, busiest day, and Timeline show **internet usage** (the internet-adapter series from §8.1), so nothing is double counted. The Adapters and Wi-Fi networks tabs show each adapter's or network's **own raw traffic**, so their rows can add up to more than the headline total (e.g. a VPN and the Wi-Fi under it both appear). Say this in a one-line note on those tabs. *(Raw numbers for the Wi-Fi networks tab are my call: they match what a metered hotspot actually carries.)*

**Summary** (the first tab, shown on open):
- Cards for Today, This week, This month. Each shows total, with ↓ received / ↑ sent underneath.
- Busiest day in the selected period.
- Top adapters and top Wi-Fi networks for the selected period.
- The coverage note from §8.2.

**Timeline:**
- A bar chart of usage over the selected period, with received and sent shown separately (stacked or side-by-side; builder's call). Hourly bars for a single day, daily bars for longer ranges, monthly bars for "This year" and "All time".
- Hourly detail is available only for the last 90 days. Older periods fall back to daily automatically.

**Adapters:**
- A table with one row per adapter: friendly name, type (Wi-Fi / Ethernet / Virtual / Other), received, sent, total for the period. Sortable by column.

**Wi-Fi networks:**
- A table with one row per Wi-Fi network name: received, sent, total for the period. Sortable.

### 9.2 Actions

- **Export to CSV:** exports the data for the current period and tab (or everything; builder's call, but make it obvious which), with one row per hour or day and columns for adapter, network, received and sent bytes.
- **Clear history:** clear all of it, or just one adapter's or one network's, with a confirmation dialog that states exactly what will be deleted.
- **Rename / hide adapters:** give an adapter a friendly name (e.g. "Work VPN") used everywhere in the app, and hide adapters from the right-click list and the report. Hidden adapters are still recorded and can be unhidden.

### 9.3 Look and feel

Native Win32 look, following the system light/dark mode where Win32 allows it. Charts are drawn by the app itself; there's no chart library or web view. The window is usable with the keyboard and works in High Contrast mode. *(My call.)*

## 10. Settings dialog (decided)

Opened from the right-click menu. It's small and has no tabs unless needed. It contains:

- Start with Windows (default on).
- Adapter: Automatic / a specific adapter (the same choice as the right-click menu).
- Units: Bits / Bytes.
- Show mini graph; Arrows / Letters.
- Show on: Primary taskbar only / All taskbars.
- Manage adapters (rename/hide). This may live here or in the report; builder's call.
- A link to open the data folder, and the version / license / source URL ("About").

## 11. Shipping

- **Installer:** a **per-user** installer with no admin prompt. MSI (e.g. WiX) or EXE setup (e.g. Inno Setup) is the builder's call; pick whichever makes the uninstall behavior below reliable and fits a winget manifest. It adds a Start menu entry. Uninstall removes the app and the autostart entry. When the uninstall is interactive, it asks whether to also delete usage history. When it's silent (`winget uninstall`, Settings > Apps without a dialog), it **keeps** history. *(My call: never destroy data without asking.)*
- **winget:** publish a manifest to `microsoft/winget-pkgs` for each release.
- **Code signing:** apply to the SignPath Foundation's free signing for open-source projects. Releases ship unsigned until approved, with SHA-256 checksums published alongside.
- **Builds:** GitHub Actions builds release artifacts from tagged commits, so anyone can see exactly how a release was produced.
- **Architectures:** x64 required; ARM64 is the builder's call (nice to have, cheap with MSVC).
- **Single instance:** launching a second copy brings up the existing instance's report window instead of starting another overlay.
- **Localization:** all user-visible strings in resource files (`.rc` string tables or equivalent). English only at launch, structured so contributors can add languages.

## 12. Repository basics

- `LICENSE` (MIT), `README.md` (what it is, screenshot, install, the privacy promise, how to build), `PRIVACY.md` or a privacy section (no network, data stays local, where it's stored), `CONTRIBUTING.md`, `SECURITY.md`.
- Build with CMake + MSVC (Visual Studio 2022 Build Tools); builder's call if another standard Windows C++ build setup fits better.
- Unit tests for the parts that must be right: rate calculation including reset/gap handling, unit formatting, hourly bucketing across DST and time-zone changes, rollup/retention, and CSV export.

## 13. Known risks and open technical questions for the builder

These are expected hard spots. They don't change *what* gets built, but the plan should de-risk them early.

1. **Drawing on the Windows 11 taskbar.** Win11 removed deskbands and its taskbar is XAML-based. The established approach (used by TrafficMonitor and similar) is a layered, topmost, non-activating window positioned over empty taskbar space next to the tray, kept in sync as the taskbar changes. On Win10, parenting into the taskbar window and reserving space is possible. Either way, the anti-goals in §4 apply (no overlap, no flicker, correct z-order vs. Start/flyouts/fullscreen). **Spike this first.**
2. **Wi-Fi network names may need location permission.** Newer Windows 11 builds gate reading the Wi-Fi SSID through the WLAN API behind the location privacy setting. Prefer a source that doesn't need it (e.g. the network name from the Network List Manager). If the SSID truly can't be read without location access, record the network as "Unknown Wi-Fi network" rather than prompting. **Verify early on Win11.**
3. **Getting space on the Win11 taskbar when it's crowded.** With many open apps and left alignment, there may be no free space by the tray. The builder decides the behavior (e.g. shrink to a narrower layout, then hide the graph), but it must never overlap.
4. **Counter behavior across sleep and adapter resets** varies by driver, especially for virtual adapters. Test with at least Wi-Fi, Ethernet, a VPN (e.g. WireGuard) and a Hyper-V/WSL vEthernet adapter.

## 14. Left to the builder

- Internal architecture, module boundaries, threading model.
- Exact Win32 APIs used, within the constraints above.
- Database schema, settings file format, flush interval.
- Exact adapter filtering rule (§7.1), chart styling, exact font and colors within "follows light/dark".
- Test framework and CI layout.
- Installer tooling.
- Product name and icon, if something better than "Network Speed Monitor" comes up (raise it; don't just change it).

## 15. Calls I made (not discussed; please check)

- Default unit is **bytes**, and units are **decimal** (1000).
- Report volumes are always in bytes, regardless of the speed-unit setting.
- In Manual mode, a missing adapter shows `—` rather than falling back to Automatic.
- The Period picker list in §9.1, and that weeks follow Windows regional settings.
- Hidden adapters keep being recorded.
- Corrupt database handling in §8.3.
- Keyboard and High Contrast support in the report window.
- The second-instance behavior in §11.
- A hover tooltip on the overlay showing the measured adapter.
- The Adapters and Wi-Fi networks tabs show raw per-adapter/per-network traffic (headline totals use the internet-adapter series).
- Installer format is the builder's choice; a silent uninstall keeps usage history.
