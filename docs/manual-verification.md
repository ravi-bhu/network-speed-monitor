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
- [ ] **Sign out or shut down** with the demo running. After signing back in, the task list has its full width (restored on WM_ENDSESSION).
- [ ] **Theme:** switch Dark/Light while it runs. The readout recolors within 1 s, with no restart.
- [ ] **Scaling:** change 125% to 150% and back while it runs. The readout is re-measured, stays crisp, sits in the right place, and the reserved space follows. Then change the resolution.
- [ ] **Toolbars:** right-click the taskbar > Toolbars > Links or Desktop. Check that the readout doesn't cover the toolbar band, since the reservation assumes the task list is the last band before the tray. Record what you see.
- [ ] **Small taskbar buttons** on and off while it runs: the readout and reserved space follow.
- [ ] **Tooltip:** hover over the readout. "Network Speed Monitor" (the demo's text) appears and goes away when the mouse leaves.
- [ ] **Tray clock:** run the demo for 3+ minutes, then exit. The tray clock updates every minute throughout. (While this task was being built, this machine's clock stayed at 1:14 AM from the first demo run onwards. Explorer was responsive and idle, and the cause is unknown. See the Task 5 report.)
- [ ] **CPU:** Task Manager > Details over 60 s is below 0.5%.

### Windows 11 (VM)
- [ ] Explorer restart, auto-hide, fullscreen (F11 and a game), two monitors, z-order and tooltip, as in the Windows 10 list. Win11 has no top/left/right taskbar and no space reservation, so there the readout relies on the UIA button extent.
- [ ] Flyouts: Start, search, quick settings, notification center/calendar, tray overflow, Task View, Alt+Tab, and the Widgets board. The readout is never drawn over them. Record the class of any flyout that covers it. The classes handled are `Windows.UI.Core.CoreWindow`, `XamlExplorerHostIslandWindow`, `MultitaskingViewFrame`, `NotifyIconOverflowWindow` and `TopLevelWindowForOverflowXamlIsland`.
- [ ] Left alignment with 30 windows: the graph drops, then the readout hides; it never overlaps a button. The readout comes back as windows close.
- [ ] **CPU of both NetworkSpeedMonitor.exe and explorer.exe** with 20–30 task buttons, over 60 s. On Win11 the UIA button query runs every 2 s while the readout shows. On Win10 (task-list root, 10–30 buttons) one query cost 5–25 ms of our CPU and roughly 25–75 ms of Explorer's. If the two together exceed 0.5% of the machine, report back.
