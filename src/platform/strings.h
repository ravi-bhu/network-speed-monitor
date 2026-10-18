#pragma once
#include <windows.h>

#include <string>

namespace nsm {

// Loads a STRINGTABLE entry from the module this code is linked into; empty if missing.
std::wstring LoadStr(UINT id);

}  // namespace nsm
