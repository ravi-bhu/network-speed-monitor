#pragma once
#include <filesystem>

namespace nsm {

// %LOCALAPPDATA%\NetworkSpeedMonitor, created if missing. Empty path on failure.
std::filesystem::path DataDir();

}  // namespace nsm
