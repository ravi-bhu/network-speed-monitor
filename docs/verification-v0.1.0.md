# Verification record: v0.1.0 (automated part)

Checked against the anti-goals in spec section 4 and the budgets in section 5.
Machine: Windows 10 Pro 22H2 (build 19045), 8 logical cores, x64. Date: 2026-10-19.
Build: Release and Debug from `main` (MSVC 2022, static CRT).

This file records only what was verified automatically. Everything that needs a human eye or a
Windows 11 machine is listed in [manual-verification.md](manual-verification.md) and is **not yet done**.

## 1. No networking imports (`tools/check_imports.ps1`)

`tools/check_imports.ps1 build\src\ui\Release\NetworkSpeedMonitor.exe` -> exit 0.
Imported DLLs: advapi32, api-ms-win-shcore-scaling-l1-1-1, comctl32, d2d1, dwmapi, dwrite, gdi32,
iphlpapi, kernel32, ole32, oleaut32, shell32, user32, uxtheme.
None of `ws2_32.dll`, `winhttp.dll`, `wininet.dll`, `urlmon.dll` is imported.
(`iphlpapi.dll` is used for adapter counters only; it opens no sockets.)

Negative self-test: `tools/check_imports.ps1 -SelfTest C:\Windows\System32\curl.exe` -> exit 0
("forbidden import detected as expected (ws2_32.dll)"). Running the plain check against curl.exe exits 1.
CI runs both after the Release build (`.github/workflows/ci.yml`); `release.yml` runs the check per exe.

## 2. Resource use (`tools/measure.ps1 -Minutes 10`)

Release build, started with no arguments (overlay only), real data folder, 2026-10-19 10:38.
Measured pid 25568, `build\src\ui\Release\NetworkSpeedMonitor.exe`, 602.5 s (10.04 min), 117 samples
(a sample is 5 s of sleep plus the CIM query, so about 5.15 s apart).

| Metric | Result | Budget |
|---|---|---|
| Average CPU (share of the whole machine, 8 cores) | 0.027 % | < 0.5 % |
| Max private bytes | 7.61 MB | <= 15 MB |
| Write bursts (samples where the process's WriteOperationCount rose) | 2 | <= 2 per 10 min |
| Total write ops / bytes written in the window | 4 / 8,240 | n/a |

Result: PASS (exit 0), with the write budget exactly met. The two bursts are consistent with the 300 s flush timer
firing twice in 10 minutes. The shutdown flush is outside the window.

Why write counts come from the process and not from file times: the database is in WAL mode and NTFS does not
refresh a file's directory-entry mtime or size while it is held open. In a 6m45s run the `usage.db-wal` entry
stayed at 32 bytes and its start time throughout, yet `usage.db` was 36,864 bytes after a graceful exit.
So no mtime or length metric can see flushes. An earlier run of this task that used `usage.db` mtime reported
0 writes; that figure was an artefact of this and is superseded by the table above.

## 3. Sockets owned by the process at sampling instants

During the same window (158 samples about every 3 s for 10 minutes, taken by a separate watcher),
`Get-NetTCPConnection -OwningProcess <pid>` and `Get-NetUDPEndpoint -OwningProcess <pid>` returned nothing: 0 TCP
and 0 UDP entries in total. This shows no sockets at the sampling instants only; a short-lived connection between
samples would be missed. The stronger evidence is section 1: the exe does not import any Winsock or HTTP DLL.

## 4. Test suites (2026-10-19)

| Build | Unit (`-tse=integration`) | Integration (`-ts=integration`) |
|---|---|---|
| Debug | 120 cases / 600 assertions, all passed | 52 cases / 429 assertions, all passed |
| Release | 120 cases / 600 assertions, all passed | 52 cases / 429 assertions, all passed |

## 5. Not covered here

The Task 5 and Task 13 manual matrices, light/dark and DPI checks, multi-monitor, and everything on
Windows 11 require a human or a Windows 11 VM. See [manual-verification.md](manual-verification.md).
