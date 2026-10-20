#pragma once
#include <windows.h>

#include <functional>

namespace nsm {

// HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
bool IsTaskbarLight();  // SystemUsesLightTheme == 1
bool AreAppsLight();    // AppsUseLightTheme == 1
bool IsHighContrast();  // SPI_GETHIGHCONTRAST

// Turns a window's dark title bar on or off with DWMWA_USE_IMMERSIVE_DARK_MODE: attribute 20 (Windows 10 20H1
// and later), or 19, its number on Windows 10 1809 to 1909, when 20 is refused. False if both are refused.
bool SetDarkTitleBar(HWND hwnd, bool dark);
// The same, calling `set` in place of DwmSetWindowAttribute (tests).
using SetWindowAttributeFn = std::function<HRESULT(HWND, DWORD, const void*, DWORD)>;
bool SetDarkTitleBar(HWND hwnd, bool dark, const SetWindowAttributeFn& set);

}  // namespace nsm
