# Manual verification

Checks that need a human eye, a system setting change, or a machine we don't have.
Tick an item and note the date, Windows build and result when you've done it.

## Task 4: Overlay window on the taskbar (demo data)

Run `build\src\ui\Debug\NetworkSpeedMonitor.exe --demo`. Left-click the readout to toggle the graph and right-click it to toggle arrows/letters. The demo has no menu yet. Since Task 5, end it with `taskkill /im NetworkSpeedMonitor.exe` (**without** `/f`) or start it with `--demo-seconds N`. A forced kill (Task Manager's End task, `Stop-Process`, `taskkill /f`) leaves the Windows 10 task list shrunk, as described under Task 5.

Already checked automatically on Windows 10 22H2 (build 19045, 125% scaling, dark taskbar at the bottom, one monitor), from screen captures: the readout sits left of the tray without overlapping it or the task buttons, both lines are readable, the graph and both toggles work, private bytes are about 6.2 MB, and CPU is about 0.25% of the machine (Debug build).

### Windows 10 (this machine or any Win10 PC)
- [ ] Light taskbar: Settings > Personalization > Colors > "Choose your default Windows mode" = Light. Restart the demo. Text is black, ↑ is orange-brown (#B35C00), ↓ is blue (#0067C0), and both are readable on the light taskbar.
- [ ] Switch back to Dark and restart the demo. Text is white, ↑ is #F0A35E, ↓ is #5EB8F0.
- [ ] At 150% scaling (Settings > Display > Scale), the text and graph are crisp, with no blur or bitmap stretching, and the readout still fits the taskbar height.
- [ ] Also check 100%, 175% and 200% scaling.
- [ ] Open about 20 windows so the task buttons approach the tray. The graph drops first, then the readout hides, and it never overlaps a task button. Close windows again; the readout comes back with its graph.
- [ ] With "Use small taskbar buttons" on (the taskbar is about 30 px at 100%), check whether the two lines still fit. The readout is 32 DIP tall and may be clipped or overhang; record what you see.
- [ ] Clicking the readout never takes focus from the active window (the caret in an open editor keeps blinking).
- [ ] After clicking an empty part of the taskbar, the readout is back in front within 1 s. The taskbar can briefly cover it; this is fully handled by Task 5's z-order work.
- [ ] Task Manager > Details > "Memory (private working set)" for NetworkSpeedMonitor.exe is under 15 MB.
- [ ] Taskbar moved to the top, left and right (unlock the taskbar and drag it). Record where the readout lands. Vertical placement is computed by `PlaceOverlay`, but the full lifecycle comes in Task 5.
- [ ] With a second monitor showing taskbars on all displays, confirm `ProbeTaskbars` sees the secondary taskbar and its clock (the `ClockButton` window). The demo only uses the primary; check with the `nsm_tests -ts=integration` MESSAGE output, which prints the number of taskbars.

### Windows 11: the spec's top risk (§13.1, §13.3), not yet verified
No Windows 11 machine or VM was available. **Do this before building on the overlay further.** Use a Hyper-V "Windows 11 dev environment" image.
- [ ] Run `nsm_tests.exe -ts=integration`. "ProbeTaskbars finds the primary taskbar" passes, and its MESSAGE shows a non-empty `occupied` extent ending right after the last task button. **If UI Automation reports no Win11 task buttons (`occupied` empty or wrong), stop and report back before continuing.**
- [ ] `TrayNotifyWnd` exists on the Win11 primary taskbar and its rect covers the tray icons and clock (the `tray` value in the same MESSAGE).
- [ ] Taskbar alignment **Center**: the demo readout sits left of the tray, with no overlap of the centered task buttons, and the graph drops or the readout hides as buttons are added.
- [ ] Taskbar alignment **Left**: same checks, including about 20 open windows.
- [ ] Light and dark modes, plus 150% scaling, as in the Win10 list.
- [ ] Private working set under 15 MB.
- [ ] Secondary monitor taskbar: the clock is found through UIA (`ClassName == "ClockButton"`, an unverified guess). If it isn't, the readout falls back to the taskbar's far right end. Record which happened.

## Task 5: Overlay lifecycle (Explorer restart, auto-hide, fullscreen, flyouts, monitors, Win10 space)

Run `build\src\ui\Debug\NetworkSpeedMonitor.exe --demo`. Add `--all-taskbars` to show the readout on every taskbar, and `--demo-seconds N` to make it exit by itself after N seconds. End it with `taskkill /im NetworkSpeedMonitor.exe` (**without** `/f`): that closes it cleanly, and the task list gets its full width back. On Windows 10 the demo shrinks Explorer's task list (`MSTaskSwWClass`) so the task buttons stop short of the readout. A forced kill skips the restore, and the task list stays short until Explorer next lays out the taskbar.

Already checked automatically on Windows 10 22H2 (build 19045, 125% scaling, dark taskbar at the bottom, one monitor), using only windows the check created and closed itself. Screen captures are in `.superpowers/sdd/2026-10-18-network-speed-monitor/shots/task5-*.png`.
- The task list ends 5 px (4 DIP) left of the readout (`MSTaskSwWClass` 180..1480, readout 1485..1644). After a clean exit (`--demo-seconds`, WM_CLOSE, `taskkill` without `/f`, or WM_ENDSESSION) it's back to 180..1649.
- With 20 extra taskbar buttons the task list shows its scroll arrows and stops before the readout.
- A borderless window covering the monitor and brought to the front hides the readout within 0.3 s. It comes back after the window closes.
- A small window resized to cover the monitor in place, without a foreground change (like F11), hides the readout within the 1 s tick. The runs measured 61 ms and 186 ms.
- A foreground window of class `MultitaskingViewFrame` hides the readout, and it comes back once that window is closed.
- A posted `TaskbarCreated` or `WM_DISPLAYCHANGE` recreates the overlay (new window), re-probes and re-reserves the space. `ImmersiveColorSet` repaints it.
- The overlay window stays above `Shell_TrayWnd` in the z-order.
- The tooltip control exists, owned by the overlay, with one tool.
- CPU over 60 s is 0.013 to 0.026% of the machine (8 logical CPUs, Debug build), with or without 20 extra taskbar buttons. Private bytes are 5.5 to 6.1 MB.

### Windows 10
- [ ] **Explorer restart:** `taskkill /f /im explorer.exe & start explorer`. While Explorer is down, the readout disappears within 1 s. After the restart it comes back within 2 s, and the task list stops short of it again.
- [ ] **Auto-hide:** Settings > Taskbar > "Automatically hide the taskbar". The readout slides out and back with the taskbar and never floats on its own. It's hidden once less than half the taskbar shows.
- [ ] **Taskbar at the top, left and right** (unlock it and drag it). The readout sits next to the tray each time, and the task buttons stop short of it (on a vertical taskbar they end above it). Move the taskbar back to the bottom; the task list is still correct.
- [ ] **Two monitors:** with "Show taskbar on all displays" on, `--demo` shows the readout only on the primary taskbar and `--demo --all-taskbars` shows it on every taskbar. A secondary taskbar gets no space reserved; there the readout uses the UIA button extent. Open windows on the secondary until its buttons come close: the graph drops first, then the readout hides, and it never overlaps a button.
- [ ] **Two monitors, fullscreen on one** (`--all-taskbars`): only the readout on that monitor hides.
- [ ] **Fullscreen video:** F11 on a YouTube page, then YouTube's own fullscreen button. The readout hides within about 1 s and returns afterwards.
- [ ] **Fullscreen game,** or another exclusive-fullscreen DirectX app: the readout hides and returns afterwards.
- [ ] **Shell flyouts:** the readout is never drawn over any of these: Start, search, Action Center, Task View (Win+Tab), Alt+Tab, the tray overflow (^), the clock/calendar, volume and network flyouts. If one does get covered, record its window class (e.g. with Spy++).
- [ ] **Z-order:** click an empty part of the taskbar, then a task button, then the desktop. The readout stays in front of the taskbar each time (it's re-raised when the foreground changes, and the 1 s tick catches the rest).
- [ ] **30 real windows,** with "Combine taskbar buttons" set to Never and then Always. The task list scrolls rather than running under the readout. Exit the demo cleanly; the task list gets its full width back.
- [ ] **Forced kill** (`taskkill /f /im NetworkSpeedMonitor.exe`): the task list stays short (a known limitation). Record what restores it (opening a window, resizing the taskbar, restarting Explorer).
- [ ] **Re-layout loop damping:** while the demo runs, add and remove tray icons and change the taskbar size a few times. The task list ends before the readout each time. If Explorer keeps undoing the reservation (more than 5 re-applies in 2 s), the demo backs off for 10 s and then tries again; record whether that ever shows.
- [ ] **Sign out or shut down** with the demo running. After signing back in, the task list has its full width (restored on WM_ENDSESSION).
- [ ] **Theme:** switch Dark/Light while it runs. The readout recolors within 1 s, with no restart.
- [ ] **Scaling:** change 125% to 150% and back while it runs. The readout is re-measured, stays crisp, sits in the right place, and the reserved space follows. Then change the resolution.
- [ ] **Toolbars:** right-click the taskbar > Toolbars > Links or Desktop. Check that the readout doesn't cover the toolbar band, since the reservation assumes the task list is the last band before the tray. Record what you see.
- [ ] **Small taskbar buttons** on and off while it runs: the readout and reserved space follow.
- [ ] **Tooltip:** hover over the readout. "Network Speed Monitor" (the demo's text) appears and goes away when the mouse leaves.
- [ ] **Tray clock:** run the demo for 3+ minutes, then exit. The tray clock updates every minute throughout. (While this task was being built, this machine's clock stayed at 1:14 AM from the first demo run onwards, with Explorer responsive and idle; the cause is unknown. After Explorer was restarted, a 200 s demo run with the reservation active kept the clock advancing (7:25 to 7:29).)
- [ ] **CPU:** Task Manager > Details over 60 s is below 0.5%.

### Windows 11 (VM)
- [ ] Explorer restart, auto-hide, fullscreen (F11 and a game), two monitors, z-order and tooltip, as in the Windows 10 list. Win11 has no top/left/right taskbar and no space reservation, so there the readout relies on the UIA button extent.
- [ ] Flyouts: Start, search, quick settings, notification center/calendar, tray overflow, Task View, Alt+Tab, and the Widgets board. The readout is never drawn over them. Record the class of any flyout that covers it. The classes handled are `Windows.UI.Core.CoreWindow`, `XamlExplorerHostIslandWindow`, `MultitaskingViewFrame`, `NotifyIconOverflowWindow` and `TopLevelWindowForOverflowXamlIsland`.
- [ ] Left alignment with 30 windows: the graph drops, then the readout hides; it never overlaps a button. The readout comes back as windows close.
- [ ] **CPU of both NetworkSpeedMonitor.exe and explorer.exe** with 20–30 task buttons, over 60 s. On Win11 the UIA button query runs every 2 s while the readout shows. On Win10 (task-list root, 10–30 buttons) one query cost 5–25 ms of our CPU and roughly 25–75 ms of Explorer's. If the two together exceed 0.5% of the machine, report back.

## Task 7
- [ ] With Wi-Fi and Ethernet both available, switch between them (disable one adapter, or unplug the cable); the app receives a network-change message and Automatic mode follows the active internet adapter.
- [ ] Connect a VPN (including a split-default one using 0.0.0.0/1 + 128.0.0.0/1); Automatic mode switches to the VPN adapter, and back after disconnecting.
- [ ] Rapid adapter toggling produces coalesced notifications (no message flood) and the app still ends on the right adapter.

## Task 8

- [ ] **Win11 24H2+ VM, location services OFF:** run the app on Wi-Fi and confirm `QueryAdapterNetworks()` still returns the Wi-Fi network name (the NLM name, not WLAN API). If the name comes back empty, the UI (Task 15) must show "Unknown Wi-Fi network" and must not prompt for location. Record the result.

## Task 13: App shell (live speeds, recording, menu, power)

Run `build\src\ui\Debug\NetworkSpeedMonitor.exe` (no arguments). It writes the `HKCU\...\Run` value `NetworkSpeedMonitor` and creates `%LOCALAPPDATA%\NetworkSpeedMonitor\usage.db` and `settings.ini`. End it with menu > Exit or `taskkill /im NetworkSpeedMonitor.exe` (**without** `/f`). Debug builds accept `--trace <file>`, which writes one CSV line per tick (`utc,mono,display_id,internet_id,valid,rx_Bps,tx_Bps`) for checks like the parity test below.

Already checked automatically on Windows 10 22H2 (build 19045, 1920×1080 at 125%, dark taskbar at the bottom, one monitor, Wi-Fi). Screen captures were taken with `SetProcessDPIAware`.
- Speed parity: with a download running, the app's displayed ↓ (from `--trace`) averaged over 60 s was within 1.6% of `Get-Counter '\Network Interface(<Wi-Fi adapter>)\Bytes Received/sec'` (1,266,901 vs 1,286,992 B/s).
- The overlay shows real speeds; the tooltip reads "Automatic: Wi-Fi", and "Ethernet" when Ethernet is pinned.
- The right-click menu shows Adapter (Automatic + every listed adapter, current one checked), Units (Bits/Bytes radio), Show graph, Use letters (U/D), Open report… (bold default), Settings…, Exit. Choosing Use letters, Bits or a pinned adapter changes the overlay at once and is written to `settings.ini`.
- A second launch exits with code 0 in about 0.15 s; there is still one process, one app window and one overlay.
- `usage.db` is written every 5 minutes and at the UTC hour boundary: in a 19-minute run the writes landed 300, 600 and 900 s after start, then at 03:00:00 UTC. The database runs in WAL mode, so between checkpoints the write lands in `usage.db-wal`; `usage.db` itself changes at a checkpoint or exit. Use `[IO.File]::GetLastWriteTime` rather than `Get-ChildItem`, whose directory entry for a file another process holds open can be stale.
- A `WM_POWERBROADCAST`/`PBT_APMSUSPEND` and a `WM_QUERYENDSESSION` sent to the app window each wrote the pending usage at once. `WM_ENDSESSION` (TRUE) wrote it, saved `settings.ini` and ended the process.
- While it runs the Win10 task list ends at 1480, left of the readout at 1485..1644. After `taskkill` (no `/f`) it's back to 180..1649.
- CPU: about 0.04% of the machine (8 logical CPUs) in both Debug (60 s) and Release (30 s). Release private working set 3.9 MB (private bytes 6.3 MB).

### Windows 10 / 11
- [ ] **Download parity by eye:** start a large download and compare the overlay's ↓ with Task Manager > Performance > the same adapter over 60 s. They agree within about 5%.
- [ ] **Wi-Fi ↔ Ethernet:** with both available, unplug Ethernet or turn Wi-Fi off. In Automatic mode the readout switches to the other adapter within 2 s and the tooltip changes to "Automatic: <adapter>".
- [ ] **VPN pinned, then disconnected:** connect a VPN, pin it in Adapter, then disconnect. The readout shows `—` for both lines and does not fall back to Automatic. Reconnect: the speed comes back.
- [ ] **VPN in Automatic:** connecting a full-tunnel VPN switches the readout and tooltip to the VPN adapter; disconnecting switches back.
- [ ] **Sleep and resume:** sleep for 2 minutes and resume. The readout shows no spike, and the next report (Task 15) shows the traffic as usual.
- [ ] **Sign out with the app running:** after signing back in, the task list has its full width and no usage is lost (the usage written just before sign-out is in the report).
- [ ] **A new Wi-Fi network:** join a network the PC hasn't seen. Within about 10 s the usage is recorded under that network (Windows names a new network a few seconds after it connects; the app re-reads networks 10 s after each change).
- [ ] **Start with Windows:** sign out and in; the app starts and shows the readout.
- [ ] **Windows 11 VM:** the menu, tooltip and a second launch behave as above.

## Task 15: Report window

Open it by left-clicking the readout, from the menu (Open report…), or by starting the exe a second time. Debug builds accept `--data-dir <folder>`, which uses that folder instead of `%LOCALAPPDATA%\NetworkSpeedMonitor` (put `StartWithWindows=0` under `[General]` in its `settings.ini` so the run doesn't register itself in HKCU Run). They also accept `--theme light|dark|contrast`, which forces the report window's look without changing Windows settings. `contrast` runs the High Contrast code path with whatever system colours are current.

Already checked automatically on Windows 10 22H2 (build 19045, 1920×1080 at 125%, India Standard Time UTC+5:30, dark apps mode with accent-coloured title bars). The checks ran against a seeded copy of the database in `%TEMP%` plus live recording. Screens were captured with `SetProcessDPIAware`, and the window was driven by posted messages:
- All four tabs, in light and dark. The light, dark and `contrast` looks were forced per window with `--theme`.
- The timeline shows stacked bars, received in the download colour under sent in the upload colour. Bars are hourly for Today (labels keep the :30 of a half-hour time zone), daily for This month and Last 30 days, and monthly for All time ("Nov 2025 … Oct" across two years).
- The bar tooltip appears with exact values. It was shown with the arrow keys on the focused chart.
- Changing the period updates the range label, the selected-period card, the timeline and both tables. Custom range… opens prefilled with the current period's dates, and the card is titled with the range.
- The Summary card for Last 30 days (57.8 GB) equals the sum of the internet rows in the CSV exported for the same period.
- Sorting works: clicking the Name header gives A to Z with the header arrow shown, and the selection survives the re-sort.
- Rename… stores a friendly name. "WSL & Docker" shows in the overlay's Adapter submenu at once, with "&" kept. A blank name restores the Windows name.
- Hide drops the adapter from the table and from the overlay's Adapter submenu. Show hidden adapters lists it as "… (hidden)", and Unhide restores it.
- Clear this network's history… shows the Delete/Cancel task dialog, with Cancel as the default. After Delete, only that network key's rows are gone from `usage_hourly` and `usage_daily`. The unknown-network row's Clear item is greyed, because clearing by an empty key would also delete every adapter's rows that have no network.
- Export CSV… opens the save dialog titled "Export usage for the selected period", with the default name `network-usage-2026-10-19.csv` and the "CSV files (*.csv)" filter. Saving to `%TEMP%` wrote a UTF-8 file with a BOM and the Task 14 header, covering exactly Sep 20 – Oct 19 for Last 30 days.
- A corrupt `usage.db` shows the "couldn't be read… kept as <path>" banner. A store that can't be opened shows the "couldn't be saved recently" banner.
- Tab order is Period → Export CSV… → Clear all history… → tabs → page (chart, or Show hidden → list), then back round. Esc closes only the report, and the app keeps running.
- Closing and reopening restores the window's last position and size (from `settings.ini` `[ReportWindow]`).
- `taskkill /im NetworkSpeedMonitor.exe` (no `/f`) with the report open ends the app.
- Private working set (Debug build): 4.2 MB before opening, 9.8 MB with the report open, and 6.3 MB 60 s after closing it. A session that also used the save dialog ended at 12.5 MB, because the shell's file dialog leaves DLLs loaded.

### Windows 10 / 11
- [ ] **Real theme switch:** with the report open, switch Settings > Colors between Dark and Light. The window, tabs, lists, buttons and chart recolour without reopening. On Windows 10 with "Show accent colour on title bars" off, the title bar is dark in dark mode.
- [ ] **High Contrast:** turn on each High Contrast theme (Black, White, #1, #2), with the report open and after reopening it. Everything uses the theme's colours. Received bars are solid and sent bars hollow with an outline. The tabs, lists, buttons and checkbox are the system's own. Text is readable everywhere.
- [ ] **Keyboard by hand:** Ctrl+Tab and Ctrl+Shift+Tab switch tabs (the automated run couldn't inject Ctrl). Alt+P focuses Period, Alt+E exports, and Alt+C clears. Shift+F10 on a table row opens its menu. Left/Right/Home/End on the focused chart move the highlighted bar and its tooltip.
- [ ] **Mouse on the chart:** hovering a bar shows its tooltip by the pointer, and it hides when the pointer leaves the chart or the window loses focus.
- [ ] **Period list with the mouse:** open the list and pick Custom range…; the dialog opens once. Picking Custom range… again while it's selected reopens the dialog with the last range. Cancel keeps the previous period.
- [ ] **Clear all history…** on a throwaway database: after Delete, every tab is empty and new traffic starts appearing within a minute.
- [ ] **Excel:** open an exported CSV in Excel. The columns are start_local, granularity, series, adapter_id, adapter_name, adapter_kind, network, received_bytes, sent_bytes, and "Café Corner, 5G"-style names stay in one cell.
- [ ] **Overlay click:** left-click the readout. The report opens in front, and a second click brings it forward instead of opening another.
- [ ] **DPI:** move the report between monitors with different scaling, or change scaling while it's open. Text and controls rescale, and the chart stays crisp.
- [ ] **Release memory:** with a Release build and your real data, open the report, visit every tab, close it, then wait a minute. Task Manager's "Memory (private working set)" is back under 15 MB.
- [ ] **Screen reader:** Narrator reads the period box, buttons, tabs and table cells. The Summary and Timeline drawings aren't exposed to UI Automation (a known gap); record how Narrator handles them.
- [ ] **taskkill while a popup shows:** with the chart's tooltip or any menu visible, `taskkill` without `/f` may hit the popup's system shadow window and do nothing. This is a known limitation, shared by the overlay's own tooltip and menu. Confirm that it works once the popup is gone.
- [ ] **Windows 11 VM:** all of the above, plus whether the Win11 dark mode draws the checkbox, combo box and scroll bars dark.

## Task 16: Settings dialog and About
- [ ] **Settings from the menu:** right-click the overlay and choose Settings…. Every control changes the overlay at once on OK (units, graph, letters, all taskbars, adapter), and Cancel or Esc changes nothing. After restarting the app the dialog shows the same values.
- [ ] **Start with Windows:** unchecking it and pressing OK removes `HKCU\...\Run\NetworkSpeedMonitor`. Checking it again and pressing OK writes it back (the automated run saw the value removed, not re-added through the dialog).
- [ ] **Manage adapters…:** the dialog closes with your changes applied, and the report opens on the Adapters tab.
- [ ] **Open data folder:** Explorer opens `%LOCALAPPDATA%\NetworkSpeedMonitor`.
- [ ] **About link:** configure with `-DNSM_SOURCE_URL=<url>`; the "Source code" link appears under the About line and opens that URL in the default browser. Without the option there is no link. The link is the system blue, which is dim on the dark dialog (about 3:1); check how it looks on your display and with a High Contrast theme.
- [ ] **Dark and High Contrast:** the dialog follows the Windows app theme. Check the check boxes and radio buttons (drawn by the app in dark mode) and the combo box on Windows 10 and Windows 11, and that High Contrast uses the system controls.
- [ ] **Pinned adapter that is hidden or unplugged:** pin an adapter, hide it in the report, and open Settings. It is still listed and selected, and OK keeps it pinned.
- [ ] **DPI:** open the dialog at 100%, 125% and 150% scaling; nothing clips, including long adapter names in the combo box.
- [ ] **Properties:** right-click `NetworkSpeedMonitor.exe` > Properties > Details shows the product name, version and copyright.
