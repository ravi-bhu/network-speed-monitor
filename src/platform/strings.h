#pragma once
#include <windows.h>

#include <initializer_list>
#include <string>

namespace nsm {

// Loads a STRINGTABLE entry from the module this code is linked into; empty if missing.
std::wstring LoadStr(UINT id);

// Replaces each "%s" in `format` with the next argument (missing ones with nothing); inserted text is not rescanned.
std::wstring FillPlaceholders(const std::wstring& format, std::initializer_list<std::wstring> args);

}  // namespace nsm
