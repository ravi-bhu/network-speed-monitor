#pragma once
#include <cstdint>
#include <string>

#include "core/types.h"

namespace nsm {

// Decimal units (1 KB = 1000 B), ~3 significant figures, auto-scaled.
std::wstring FormatSpeed(double bytesPerSec, UnitMode mode);
// The same value without the space and "/s" ("15.8MB", "9.92Mb"), for taskbars too narrow for FormatSpeed.
std::wstring FormatSpeedCompact(double bytesPerSec, UnitMode mode);
std::wstring FormatVolume(uint64_t bytes);

}  // namespace nsm
