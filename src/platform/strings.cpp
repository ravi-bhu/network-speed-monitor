#include "platform/strings.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace nsm {

std::wstring LoadStr(UINT id) {
    const wchar_t* text = nullptr;
    // cchBufferMax == 0 returns a read-only pointer into the resource (not null-terminated).
    const int len = LoadStringW(reinterpret_cast<HINSTANCE>(&__ImageBase), id, reinterpret_cast<LPWSTR>(&text), 0);
    if (len <= 0 || !text) return {};
    return std::wstring(text, static_cast<size_t>(len));
}

std::wstring FillPlaceholders(const std::wstring& format, std::initializer_list<std::wstring> args) {
    std::wstring out;
    out.reserve(format.size());
    auto next = args.begin();
    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] == L'%' && i + 1 < format.size() && format[i + 1] == L's') {
            if (next != args.end()) out += *next++;
            ++i;
        } else {
            out += format[i];
        }
    }
    return out;
}

}  // namespace nsm
