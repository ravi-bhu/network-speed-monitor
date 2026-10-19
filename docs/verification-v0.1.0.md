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

Release build, started with no arguments (overlay only), real data folder, 120 samples at 5 s.

| Metric | Result | Budget |
|---|---|---|
| Average CPU (share of the whole machine) | 0.031 % | < 0.5 % |
| Max private bytes | 7.65 MB | <= 15 MB |
| `usage.db` LastWriteTime changes in 10 min | 0 | <= 2 |

Result: PASS (exit 0). The 0 writes is as expected for an idle overlay: no flush touched
`usage.db` during the window. The shutdown flush wrote the file when the app was closed afterwards.

## 3. Network silence

During the same 10-minute window (169 samples, one every ~3 s, 11 minutes of sampling),
`Get-NetTCPConnection -OwningProcess <pid>` and `Get-NetUDPEndpoint -OwningProcess <pid>` returned
nothing: 0 TCP and 0 UDP entries in total.

## 4. Test suites (2026-10-19)

| Build | Unit (`-tse=integration`) | Integration (`-ts=integration`) |
|---|---|---|
| Debug | 120 cases / 600 assertions, all passed | 52 cases / 429 assertions, all passed |
| Release | 120 cases / 600 assertions, all passed | 52 cases / 429 assertions, all passed |

## 5. Not covered here

The Task 5 and Task 13 manual matrices, light/dark and DPI checks, multi-monitor, and everything on
Windows 11 require a human or a Windows 11 VM. See [manual-verification.md](manual-verification.md).
