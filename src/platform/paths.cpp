#include "platform/paths.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace nsm {

std::filesystem::path DataDir() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw))) {
        CoTaskMemFree(raw);
        return {};
    }
    std::filesystem::path dir = std::filesystem::path(raw) / L"NetworkSpeedMonitor";
    CoTaskMemFree(raw);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec && !std::filesystem::is_directory(dir)) return {};
    return dir;
}

}  // namespace nsm
