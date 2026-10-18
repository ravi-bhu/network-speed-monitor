# Manual verification

Checks that need a human eye, a system setting change, or a machine we don't have.
Tick an item and note the date, Windows build and result when you've done it.

## Task 4: Overlay window on the taskbar (demo data)

Run `build\src\ui\Debug\NetworkSpeedMonitor.exe --demo`. Left-click the readout to toggle the graph and right-click it to toggle arrows/letters. The demo has no menu yet, so end it from Task Manager (or `Stop-Process -Name NetworkSpeedMonitor`).

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
