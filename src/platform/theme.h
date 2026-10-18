#pragma once

namespace nsm {

// HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
bool IsTaskbarLight();  // SystemUsesLightTheme == 1
bool AreAppsLight();    // AppsUseLightTheme == 1
bool IsHighContrast();  // SPI_GETHIGHCONTRAST

}  // namespace nsm
