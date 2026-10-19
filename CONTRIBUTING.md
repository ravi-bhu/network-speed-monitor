# Contributing

Thanks for helping. The app is a native Win32 C++20 program, built with CMake and MSVC, with a static CRT and no
runtime to install.

## Build

You need Visual Studio 2022 or the Build Tools (C++ workload) and CMake 3.25 or newer. In a Developer PowerShell:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Add `-DNSM_SOURCE_URL=https://github.com/<owner>/<repo>` to the configure step to get the "Source code" link in
the About box. For ARM64 use `-A ARM64` in a separate build folder.

## Test

Tests use doctest and are all linked into one executable. Win32-dependent tests are in the `integration` suite.

```
build\tests\Debug\nsm_tests.exe -tse="integration"     # unit tests
build\tests\Debug\nsm_tests.exe -ts="integration"      # integration tests
```

CI runs both on x64 and builds ARM64. Things that need a human (taskbar layouts, DPI, dark mode, the installer)
are listed in [docs/manual-verification.md](docs/manual-verification.md).

## Rules for changes

- **Strings go in `res/strings.rc`.** Every user-visible string lives in the `STRINGTABLE` and is loaded with
  `nsm::LoadStr(id)`, so the app can be translated. Unit suffixes (`KB/s`, `Mbps`, `GB`) are the only literals
  allowed in code.
- **The app makes no network connections, ever.** Don't add code that opens sockets, HTTP requests, update
  checks or telemetry, and don't link `ws2_32`, `winhttp`, `wininet` or `urlmon`. CI checks the executable's
  imports and fails if one appears.
- Standard user only: the manifest stays `asInvoker`; never ask for elevation.
- Win32 only: GDI, Direct2D and DirectWrite for drawing. No WebView, .NET, Qt, MFC/ATL or chart libraries.
- Units are decimal (1 KB = 1000 B). Code lives in namespace `nsm`.
- Add or update tests for logic you change (rate calculation, formatting, bucketing, rollup, CSV).

Please keep pull requests focused, and open an issue first for anything large.
