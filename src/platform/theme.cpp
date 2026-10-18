#include "platform/theme.h"

#include <windows.h>

namespace nsm {
namespace {

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

}  // namespace nsm
