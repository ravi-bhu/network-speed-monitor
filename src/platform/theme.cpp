#include "platform/theme.h"

#include <windows.h>
#include <dwmapi.h>

namespace nsm {
namespace {

constexpr DWORD kDwmUseImmersiveDarkMode = 20;           // DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 20H1+)
constexpr DWORD kDwmUseImmersiveDarkModeBefore20H1 = 19;  // its number on Windows 10 1809-1909

bool PersonalizeFlag(const wchar_t* name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS rc = RegGetValueW(HKEY_CURRENT_USER,
                                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                    name, RRF_RT_REG_DWORD, nullptr, &value, &size);
    return rc == ERROR_SUCCESS && value == 1;
}

}  // namespace

bool IsTaskbarLight() { return PersonalizeFlag(L"SystemUsesLightTheme"); }

bool AreAppsLight() { return PersonalizeFlag(L"AppsUseLightTheme"); }

bool IsHighContrast() {
    HIGHCONTRASTW hc{sizeof(hc)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

bool SetDarkTitleBar(HWND hwnd, bool dark) {
    return SetDarkTitleBar(hwnd, dark, [](HWND w, DWORD attribute, const void* value, DWORD size) {
        return DwmSetWindowAttribute(w, attribute, value, size);
    });
}

bool SetDarkTitleBar(HWND hwnd, bool dark, const SetWindowAttributeFn& set) {
    const BOOL value = dark ? TRUE : FALSE;
    for (const DWORD attribute : {kDwmUseImmersiveDarkMode, kDwmUseImmersiveDarkModeBefore20H1}) {
        if (SUCCEEDED(set(hwnd, attribute, &value, sizeof(value)))) return true;
    }
    return false;
}

}  // namespace nsm
