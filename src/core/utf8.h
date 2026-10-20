#pragma once
#include <string>
#include <string_view>

namespace nsm {

// UTF-16 (a Windows wide string) to UTF-8. Empty for empty input or when Windows can't convert it.
std::string ToUtf8(std::wstring_view w);

}  // namespace nsm
