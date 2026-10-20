# Manual verification

Checks that need a human eye, a system setting change, or a machine we don't have.
Tick an item and note the date, Windows build and result when you've done it.

## Session 2026-10-19: installed build on the developer's PC

Windows 10 Pro 22H2 (19045), 1920×1080 at 125%, dark mode with an accent-coloured (tan) taskbar, Wi-Fi, IST (UTC+5:30). The build is installed with the x64 setup from `dist\`. The app was driven by mouse and keyboard; installer runs went through the shell.

Passed:
- **Install:** per-user, into `%LOCALAPPDATA%\Programs\NetworkSpeedMonitor`, with an uninstall entry, a Start menu shortcut and the Run value. VERSIONINFO shows "Network Speed Monitor 0.1.0, Copyright (c) 2026 Ravi Bhushan". The running app has 0 TCP/UDP sockets.
- **Overlay:** sits left of the tray, readable, graph drawn.
  - Left-click opens the report.
  - The right-click menu has every item, and Use letters and Units › Bits apply at once and are written to `settings.ini`.
  - Bits mode shows "23.2 Kbps" with no clipping.
- **Report:** all four tabs work.
  - Summary has the cards, busiest day, top adapters, top Wi-Fi network and coverage note.
  - The Timeline is daily for This month. For Today it shows **24 hourly bars from 12 AM in IST, with no 11:30 PM bar (F4)**.
  - Adapters and Wi-Fi tables are correct.
  - **Ctrl+Tab** switches tabs and wraps from Wi-Fi networks back to Summary.
- **Settings dialog:** reflects the current settings. Esc changes nothing.
- **Fullscreen:** a borderless window covering the monitor hides the readout 50 ms after it takes the foreground, and the readout is back about 2 ms after it closes.
- **Silent upgrade with the app running (F2):** `setup.exe /VERYSILENT /SUPPRESSMSGBOXES` logged "Network Speed Monitor was running; asked it to close." The app exited by itself, the files were replaced, and the new version started (a new process). The Run value was kept.
- **Silent uninstall with the app running (F2):** the app closed by itself. The program folder, Start menu entry, uninstall key and Run value are gone; `usage.db` and `settings.ini` are kept. The WAL file was checkpointed, so the app flushed before it exited.
- **Silent reinstall (F2):** the app was running about 6 s later, and the Run value and Start menu entry are back.

Found (filed as issues):
- In dark mode the report's "Show hidden adapters" checkbox draws as a solid white box. The Settings dialog custom-draws its check boxes, but the report window doesn't (#26). PR #55 draws it dark; see "UI fixes" below.
- On an accent-coloured (light tan) taskbar the readout uses the dark-taskbar palette: white text and an orange graph with low contrast. The palette follows the light/dark *mode*, not the taskbar's actual colour.
- When the period is This month (or Today or This week), the period card duplicates the matching fixed card (#28). PR #56 drops the duplicate card; see "UI fixes" below.
- The overlay tooltip didn't appear on synthetic mouse hover. Check it with a real mouse before treating it as a bug.

Guided checks (the user changed the setting, then the app was checked by screenshot and window geometry):
- **Explorer restart:** pass. The readout was back about 4 s after `explorer.exe` was killed (mostly Explorer's own startup), in the same app process. The task list ended 5 px before it.
- **Tooltip:** shows "Automatic: Wi-Fi" on a real mouse hover. Synthetic hover didn't trigger it. It stayed light in dark mode (#29). PR #57 makes it follow the taskbar's mode; see "UI fixes" below.
- **Light mode:** pass. Switched live, with no restart, to dark text, ↑ `#B35C00` and ↓ `#0067C0`, readable. The dark-mode "tan" taskbar was wallpaper transparency, not an accent colour, and white text there matches the system clock (#27, closed).
- **Crowding, Combine: Never:** pass. With 10 and then 25 Notepad windows, the task list stopped at 180..1480 with paging arrows. The readout stayed at 1485..1644 with its graph, with no overlap.
- **Taskbar at the top:** pass (task list 180..1480, readout 1485..1644, vertically centred).
- **Left or right at the default width (87 px at 125%):** failed at first; the readout was hidden because the narrowest layout didn't fit (#30). **Fixed and re-verified the same day:** a compact readout (`↑ 1.2KB`) now shows above the tray. Widened to 300 px, both sides work with the full layout and graph, and the task list reserves room.
- **Auto-hide:** failed at first; the readout never came back when the taskbar slid in (#31). There were two causes: the slide raises no location event, and with auto-hide a focused maximized app covers the monitor and was taken for a fullscreen app. **Fixed and re-verified the same day** on the left taskbar with auto-hide. Note: while Claude drives the screen, its own full-screen overlay is the foreground window and correctly hides the readout, so check auto-hide by hand.

Still open on Windows 10: scaling 100/150/175/200%, High Contrast, two monitors, F11/game fullscreen, shell flyouts by hand, sleep/resume, sign-out, the elevated-instance check (F1), the interactive uninstall prompt, Wi-Fi/Ethernet/VPN switching, and Narrator.

## Task 4: Overlay window on the taskbar (demo data)

Run `build\src\ui\Debug\NetworkSpeedMonitor.exe --demo`. Left-click the readout to toggle the graph and right-click it to toggle arrows/letters. The demo has no menu yet. Since Task 5, end it with `taskkill /im NetworkSpeedMonitor.exe` (**without** `/f`) or start it with `--demo-seconds N`. A forced kill (Task Manager's End task, `Stop-Process`, `taskkill /f`) leaves the Windows 10 task list shrunk, as described under Task 5.

Already checked automatically on Windows 10 22H2 (build 19045, 125% scaling, dark taskbar at the bottom, one monitor), from screen captures: the readout sits left of the tray without overlapping it or the task buttons, both lines are readable, the graph and both toggles work, private bytes are about 6.2 MB, and CPU is about 0.25% of the machine (Debug build).

### Windows 10 (this machine or any Win10 PC)
- [ ] Light taskbar: Settings > Personalization > Colors > "Choose your default Windows mode" = Light. Restart the demo. Text is black, ↑ is orange-brown (#B35C00), ↓ is blue (#0067C0), and both are readable on the light taskbar.
- [ ] Switch back to Dark and restart the demo. Text is white, ↑ is #F0A35E, ↓ is #5EB8F0.
- [ ] At 150% scaling (Settings > Display > Scale), the text and graph are crisp, with no blur or bitmap stretching, and the readout still fits the taskbar height.
- [ ] Also check 100%, 175% and 200% scaling.
- [ ] Open about 20 windows so the task buttons approach the tray. The graph drops first, then the readout hides, and it never overlaps a task button. Close windows again; the readout comes back with its graph.
- [ ] With "Use small taskbar buttons" on (the taskbar is about 30 px at 100%), check whether the two lines still fit. The readout is 32 DIP tall; `PlaceOverlay` clamps it to the bar's thickness (#4), so it no longer overhangs but the lines may be clipped by a pixel or two. Record what you see.
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
- [ ] **Forced kill** (`taskkill /f /im NetworkSpeedMonitor.exe`): the task list stays short (a known limitation). Record what restores it (opening a window, resizing the taskbar, restarting Explorer). Since PR #51, starting the demo again and exiting it cleanly also restores it (#9).
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
- [ ] **Excel:** open an exported CSV in Excel. The columns are start_local, granularity, series, adapter_id, adapter_name, adapter_kind, network, received_bytes, sent_bytes, start_utc, and "Café Corner, 5G"-style names stay in one cell.
- [ ] **Overlay click:** left-click the readout. The report opens in front, and a second click brings it forward instead of opening another.
- [ ] **DPI:** move the report between monitors with different scaling, or change scaling while it's open. Text and controls rescale, and the chart stays crisp.
- [ ] **Release memory:** with a Release build and your real data, open the report, visit every tab, close it, then wait a minute. Task Manager's "Memory (private working set)" is back under 15 MB.
- [ ] **Screen reader:** Narrator reads the period box, buttons, tabs and table cells. The Summary and Timeline drawings aren't exposed to UI Automation (a known gap); record how Narrator handles them.
- [ ] **taskkill while a popup shows:** with the chart's tooltip or any menu visible, `taskkill` without `/f` may hit the popup's system shadow window and do nothing. This is a known limitation, shared by the overlay's own tooltip and menu. Confirm that it works once the popup is gone.
- [ ] **Windows 11 VM:** all of the above, plus whether the Win11 dark mode draws the combo box and scroll bars dark. (The checkbox is drawn by the app since #26.)

## Task 16: Settings dialog and About
- [ ] **Settings from the menu:** right-click the overlay and choose Settings…. Every control changes the overlay at once on OK (units, graph, letters, all taskbars, adapter), and Cancel or Esc changes nothing. After restarting the app the dialog shows the same values.
- [ ] **Start with Windows:** unchecking it and pressing OK removes `HKCU\...\Run\NetworkSpeedMonitor`. Checking it again and pressing OK writes it back (the automated run saw the value removed, not re-added through the dialog).
- [ ] **Manage adapters…:** the dialog closes with your changes applied, and the report opens on the Adapters tab.
- [ ] **Open data folder:** Explorer opens `%LOCALAPPDATA%\NetworkSpeedMonitor`.
- [ ] **About link:** configure with `-DNSM_SOURCE_URL=<url>`; the "Source code" link appears under the About line and opens that URL in the default browser. Without the option there is no link. In dark mode the link uses the accent colour, a light blue that reads well on the dark dialog (#22); in light mode it's the system blue. Check how it looks with a High Contrast theme.
- [ ] **Dark and High Contrast:** the dialog follows the Windows app theme. Check the check boxes and radio buttons (drawn by the app in dark mode) and the combo box on Windows 10 and Windows 11, and that High Contrast uses the system controls.
- [ ] **Pinned adapter that is hidden or unplugged:** pin an adapter, hide it in the report, and open Settings. It is still listed and selected, and OK keeps it pinned.
- [ ] **DPI:** open the dialog at 100%, 125% and 150% scaling; nothing clips, including long adapter names in the combo box.
- [x] **Properties:** right-click `NetworkSpeedMonitor.exe` > Properties > Details shows the product name, version and copyright.

## Task 17: Installer, winget, release pipeline, docs
- [x] **Compile the x64 installer:** done 2026-10-19 on Win10 with a per-user Inno Setup 6 install (`%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe`). `ISCC.exe /DMyAppVersion=0.1.0 /DMyArch=x64 /DSourceExe=<repo>\build\src\ui\Release\NetworkSpeedMonitor.exe installer\NetworkSpeedMonitor.iss` succeeded, including the `[Code]` section, and wrote `dist\NetworkSpeedMonitor-0.1.0-x64-setup.exe` (2.8 MB).
- [x] **Compile the arm64 installer** against an ARM64 build. The release workflow does this in CI. _Done 2026-10-19 by the v0.1.0 release run (39276217890)._
- [ ] **Install as a standard user:** run the installer from a non-admin account. There is no UAC prompt, it installs to `%LOCALAPPDATA%\Programs\NetworkSpeedMonitor`, adds a Start menu entry, and "Launch Network Speed Monitor" on the last page starts the app.
- [ ] **Autostart:** after install, sign out and back in; the overlay appears. (Settings > Start with Windows is on by default.)
- [ ] **Running app:** with the app running, run the installer again (upgrade) and the uninstaller. Each closes the app by itself, without asking and without killing it (see the F2 rows under "Final review fixes"); the upgrade starts it again.
- [ ] **Interactive uninstall:** Settings > Apps > Uninstall asks "Also delete your usage history?" (default No). Yes removes `%LOCALAPPDATA%\NetworkSpeedMonitor`; No keeps it. In both cases the app folder, Start menu entry and `HKCU\...\Run\NetworkSpeedMonitor` are gone.
- [x] **Silent uninstall:** `unins000.exe /VERYSILENT` shows no prompt, removes the app and the Run value, and keeps `%LOCALAPPDATA%\NetworkSpeedMonitor`.
- [ ] **winget:** fill in a copy of `packaging\winget\0.1.0` with real values and run `winget validate <folder>`; then `winget install --manifest <folder>` and `winget uninstall NetworkSpeedMonitor.NetworkSpeedMonitor` on a test machine. (The templates with sample values already pass `winget validate` on winget 1.29.)
- [x] **Release workflow:** push a test tag such as `v0.1.0` on a fork. "Check imports" runs `tools/check_imports.ps1` on both exes and passes; the run produces both installers, `SHA256SUMS.txt` and a GitHub Release with all three files attached. The disabled SignPath steps are skipped. _Done 2026-10-19 on the real repository: the v0.1.0 run passed every step, skipped the four SignPath steps, and the downloaded installers match `SHA256SUMS.txt`._
- [ ] **README:** the screenshot renders on GitHub and the docs read correctly (README, PRIVACY, CONTRIBUTING, SECURITY, THIRD_PARTY_NOTICES, docs/signing.md).
- [ ] **Windows 11 VM:** repeat the install, autostart and both uninstall checks.

## Final review fixes
- [ ] **Elevated first instance (F1):** start `NetworkSpeedMonitor.exe` with "Run as administrator", then start it again normally. The second copy exits at once: one overlay, and usage is not double counted. (The report does not open, because Windows blocks the message to the elevated copy.) Exit the elevated copy afterwards.
- [ ] **Half-hour time zone (F4):** set Windows to (UTC+05:30) Chennai, Kolkata, Mumbai, New Delhi and run the app across local midnight. After 00:00, the report's Today shows the new traffic, and its Timeline has 24 bars from 12 AM with no 11:30 PM bar. Repeat with (UTC+05:45) Kathmandu if you can.
- [x] **winget upgrade with the app running (F2, Inno Setup was not available to compile this):** with the app running and some traffic since the last flush, run `winget upgrade` (or the new installer with `/VERYSILENT`). No message box appears; the overlay disappears within a few seconds (the app exits through WM_CLOSE, so it saves the usage it held and, on Windows 10, gives the task list its space back), the files are replaced, and the new version starts by itself. Nothing is killed, and the report still has the traffic from just before the upgrade.
- [x] **winget uninstall with the app running (F2):** with the app running, run `winget uninstall NetworkSpeedMonitor.NetworkSpeedMonitor` (or `unins000.exe /VERYSILENT`). No prompt; the app exits, the app folder and `HKCU\...\Run\NetworkSpeedMonitor` are gone, and `%LOCALAPPDATA%\NetworkSpeedMonitor` is kept.
- [x] **Silent fresh install starts the app (F2):** on a machine without the app, run the installer with `/VERYSILENT` (or `winget install`). The overlay appears without a sign-out, and `HKCU\...\Run\NetworkSpeedMonitor` exists afterwards (the app writes it on its first run).
- [ ] **Interactive uninstall still asks about history (F2):** with the app running, uninstall from Settings > Apps. The app closes by itself, and the uninstaller still asks "Also delete your usage history?" (default No). The app is closed as the uninstaller starts, so cancelling at its "Are you sure" prompt leaves the app closed; start it again from the Start menu.
- [x] **Setup log (F2):** run the installer with `/LOG=setup.log` while the app runs; the log has "Network Speed Monitor was running; asked it to close."

## UI fixes (PRs #48-#57)

Run these on a build with all ten PRs merged. Each check applies to Windows 10 and Windows 11 unless its heading or label says otherwise. The issue each one covers is in brackets.

### Overlay
- [ ] **Clicks:** left-click the readout; the report opens once. Right-click it; the menu opens at the pointer and its items work. (#6)
- [ ] **Drags aren't clicks:** press a mouse button on the desktop, drag onto the readout and release: nothing happens. Press on the readout, drag off it and release: nothing happens. Try both directions with the left and the right button. (#6)
- [ ] **Start menu during a click:** press on the readout, press the Windows key while still holding the button (Start opens), then release. No click is acted on (no report, no menu), and later clicks work as usual. (#6)
- [ ] **Live theme switch:** set the Windows mode to Dark and hover the readout; the tooltip is dark. Turn on Use letters, then switch to Light while the app runs. The text, both arrows, the grey U:/D: labels and the tooltip all change at once, with no restart. Switch back to Dark; they all go dark again. (#7, #29)
- [ ] **Letter labels:** with Use letters on, U: and D: are grey, the values are white on a dark taskbar and black on a light one, and the space after each label isn't coloured. (#7)
- [ ] **Sharpness:** at 125% and at 150% scaling, both lines are equally sharp. (#7)
- [ ] **High Contrast tooltip:** turn on a High Contrast theme and hover the readout. The tooltip uses the theme's colours. (#29)
- [ ] **Secondary taskbars:** with two monitors and `--all-taskbars`, click the secondary taskbar again and again. The readouts don't flicker, and on Windows 10 the task buttons don't jump. (#8)
- [ ] **Shell popups on top:** the taskbar's right-click menu, a jump list and a tray icon's tooltip next to the readout all draw over it. (#8)
- [ ] **Fullscreen, then another desktop:** play a video fullscreen on monitor A (its readout hides), then press Win+Ctrl+Right to switch to an empty virtual desktop (create one first if needed). A's readout is back within about 1 s. (#8)

### Taskbar space (Windows 10 only)
Run `--demo` as in Task 5.
- [ ] **Forced kill, then a clean run:** end the demo with `taskkill /f /im NetworkSpeedMonitor.exe`; the task list stays short. Start `--demo` again and exit it cleanly: the task list gets its full width back, up to the tray. (#9)
- [ ] **Links toolbar:** while the demo runs, turn on Toolbars > Links and drag it to the left of the task buttons. Exit cleanly: the task list ends at the tray. (#9)
- [ ] **Back-off:** add and remove tray icons and resize the taskbar quickly, many times. If the app backs off, the readout shrinks, drops its graph or hides within 1–2 s, and it never covers a task button. Once things settle, the task list ends before the readout. (#9)
- [ ] **Fast display changes:** change the resolution or the scaling 3–4 times in quick succession. Each time, the task list ends before the readout straight away. (#9)
- [ ] **Frozen Explorer (optional, needs a debugger):** attach a debugger such as WinDbg to `explorer.exe` and break into it, then exit the app with `taskkill /im NetworkSpeedMonitor.exe` (no `/f`) from a command prompt. The app exits at once instead of hanging. After you resume Explorer, the task list has its full width. (#8)

### Report window
- [ ] **Summary cards:** with Period = This month there are three cards, This month is outlined, and there's no fourth card. Today and This week: three cards, with the matching one outlined. Yesterday, Last month and a Custom range: four cards, with the fourth outlined. Check in light, dark and High Contrast. A custom range equal to today still shows four cards; that case was left as it was. (#28)
- [ ] **Summary card widths:** with three cards showing, resize the window narrow and wide. The cards share the width evenly and never overlap. (#28)
- [ ] **Maximized is remembered:** move the report, maximize it, close it and reopen it: it opens maximized. Restore it: it goes back to where you moved it. On Windows 10, repeat with the taskbar on the left; the restored window isn't shifted. (#20)
- [ ] **Maximized across a restart:** maximize the report, minimize it, choose Exit from the overlay menu, start the app and open the report: it's maximized. Then maximize, restore, close and reopen it: it opens at its normal size. (#20)
- [ ] **Opening the report:** start the exe a second time; the report comes up. With Settings… or Export CSV… open, left-click the readout; the report opens (or comes to the front) once that dialog closes. (#20)
- [ ] **Row menu from the keyboard:** on the Adapters tab, select a row and press Shift+F10, then the Menu key. Each time the row's menu opens under the row's label. (#20)
- [ ] **Dark checkbox:** in dark mode at 100% and 125% scaling, look at Show hidden adapters on the Adapters tab. The box is dark with a grey outline and is never white. Ticked, it's a blue box with a white tick. Hovering turns the outline blue, and tabbing to it puts the focus rectangle round the label. (#26)
- [ ] **Light and High Contrast checkbox:** in light mode it's the system check box, and in High Contrast it's the system's High Contrast check box. (#26)
- [ ] **Dark title bars:** set apps to Dark and open the report, then Rename… and Custom range…. Each title bar is dark. Switch to Light while each is open; its title bar turns light. Check on Windows 10 1809–1909 (which needs an older setting) as well as Windows 10 20H1 or later and Windows 11. (#21)
- [ ] **Chart tooltip in dark mode:** set apps to Dark, open the Timeline and hover a bar. The tooltip is dark. It hasn't been checked on Windows 10 22H2: if it stays light, record the Windows build, because it needs a follow-up (custom-drawing the tooltip). (#29)
- [ ] **Short hour at a clock change:** with a Debug build and a throwaway `--data-dir`, turn off "Set time automatically", set the time zone to (UTC+12:45) Chatham Islands and the date to 27 September 2026, when its clocks go forward at 2:45 AM. Open the report on Today and hover the 2 AM bar: its tooltip reads "2:00 AM – 2:45 AM". Put the date and time zone back afterwards. (#21)

### Settings dialog
- [ ] **About link:** in a build configured with `-DNSM_SOURCE_URL=<url>`, open Settings in dark mode. The Source code link is light blue (the accent colour), is underlined on hover and opens the URL. In light mode it's the system blue. (#22)
- [ ] **No source URL:** in a build without `NSM_SOURCE_URL` there's no link row. OK and Cancel sit centred, one line below the About text, with no gap left for the link. (#22)
- [ ] **Settings keeps the report's place:** with the report open, open Settings…, then move and resize the report. Change Units in Settings and press OK: the units change. Restart the app; the report opens at the new position and size. Repeat with the report maximized; after OK and a restart it opens maximized. (#22, #20)
- [ ] **Dark check boxes:** in dark mode the unchecked boxes have a visible grey outline, and the check boxes and radio buttons otherwise look as before. (#22, #26)
- [ ] **Rejected source URL (optional, build only):** configure with a URL that isn't a plain http(s) address, for example in PowerShell `cmake -S . -B build-bad '-DNSM_SOURCE_URL=https://example.com/a"b'`. The configure step stops with an error. (#22)
