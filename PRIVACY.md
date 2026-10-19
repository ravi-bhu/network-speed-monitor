# Privacy

Network Speed Monitor keeps everything on your PC.

- **No network connections.** The app never connects to the internet or any other computer. It only reads the
  byte counters Windows already keeps for your network adapters. It does not import any networking library
  (Winsock, WinHTTP, WinINet or URLMon), and releases are checked for that.
- **No telemetry.** No analytics, no crash reporting, no update checks, no accounts.
- **Where your data is.** A usage history database (`usage.db`) and a settings file (`settings.ini`) in
  `%LOCALAPPDATA%\NetworkSpeedMonitor`. The history holds bytes received and sent per hour (rolled up to days after
  90 days), with the adapter name and type, and the name Windows gives the network it was connected to (for
  Wi-Fi usually the SSID; wired and other networks get a name from Windows too) where Windows provides one.
  It does not record what you browse, which apps used the traffic, or any content.
- **Damaged history.** If the database is ever found damaged, the app moves it aside as
  `usage.corrupt-<date and time>.db` in the same folder and starts a new one. Those copies stay there until you
  delete them.
- **Start with Windows** is a value named `NetworkSpeedMonitor` under
  `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`. It needs no administrator rights.
- **Exporting** a report to CSV writes a file only where you choose to save it.

## Deleting your data

- In the app: open the report, then **Clear all history…**. Clearing a single adapter or Wi-Fi network is in the
  Adapters and Wi-Fi networks tabs (right-click a row).
- With the uninstaller: an interactive uninstall asks whether to also delete your usage history. A silent
  uninstall (for example `winget uninstall`) always keeps it.
- By hand: exit the app, then delete the folder `%LOCALAPPDATA%\NetworkSpeedMonitor`.
