#pragma once
#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

#include "core/types.h"

namespace nsm {

struct Settings {
    bool startWithWindows = true;
    std::wstring adapterId;  // empty = Automatic
    UnitMode units = UnitMode::Bytes;
    bool showGraph = true;
    bool letters = false;
    bool allTaskbars = false;
    std::optional<RECT> reportRect;

    friend bool operator==(const Settings& a, const Settings& b) {
        auto rectEq = [](const std::optional<RECT>& x, const std::optional<RECT>& y) {
            if (x.has_value() != y.has_value()) return false;
            return !x || (x->left == y->left && x->top == y->top && x->right == y->right &&
                          x->bottom == y->bottom);
        };
        return a.startWithWindows == b.startWithWindows && a.adapterId == b.adapterId &&
               a.units == b.units && a.showGraph == b.showGraph && a.letters == b.letters &&
               a.allTaskbars == b.allTaskbars && rectEq(a.reportRect, b.reportRect);
    }
};

// Missing file or invalid values fall back to defaults per key.
Settings LoadSettings(const std::filesystem::path& ini);
// Writes the whole file atomically (temp file + replace). Returns false on failure.
bool SaveSettings(const std::filesystem::path& ini, const Settings& s);

}  // namespace nsm
