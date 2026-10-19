#include "platform/autostart.h"

#include <windows.h>

#include <vector>

namespace nsm {
namespace {

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool CurrentExePath(std::wstring& out) {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return false;
        if (n < buf.size() && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            out.assign(buf.data(), n);
            return true;
        }
        if (buf.size() > 65536) return false;
        buf.resize(buf.size() * 2);
    }
}

}  // namespace

bool SetAutostart(bool enable, const std::wstring& valueName) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return false;
    bool ok;
    if (enable) {
        std::wstring exe;
        ok = CurrentExePath(exe);
        if (ok) {
            std::wstring data = L"\"" + exe + L"\"";
            ok = RegSetValueExW(key, valueName.c_str(), 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(data.c_str()),
                                static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t))) ==
                 ERROR_SUCCESS;
        }
    } else {
        LSTATUS r = RegDeleteValueW(key, valueName.c_str());
        ok = (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(key);
    return ok;
}

bool IsAutostartEnabled(const std::wstring& valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    LSTATUS r = RegQueryValueExW(key, valueName.c_str(), nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return r == ERROR_SUCCESS && type == REG_SZ;
}

}  // namespace nsm
