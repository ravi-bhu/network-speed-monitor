#pragma once
#include <string>

namespace nsm {

// HKCU\Software\Microsoft\Windows\CurrentVersion\Run\<valueName> = "<exe path>" (quoted).
// Disabling a value that does not exist counts as success.
bool SetAutostart(bool enable, const std::wstring& valueName = L"NetworkSpeedMonitor");
bool IsAutostartEnabled(const std::wstring& valueName = L"NetworkSpeedMonitor");

}  // namespace nsm
