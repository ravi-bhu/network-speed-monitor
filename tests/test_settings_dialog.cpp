#include "doctest.h"

#include "ui/dialogs.h"

#include <algorithm>
#include <cmath>

#include "ui/report_paint.h"

using namespace nsm;

namespace {

const std::vector<AdapterChoice> kChoices = {{L"{a}", L"Wi-Fi"}, {L"{b}", L"Ethernet"}};

// WCAG 2.x contrast ratio of two sRGB colours.
double Contrast(COLORREF a, COLORREF b) {
    auto luminance = [](COLORREF c) {
        auto channel = [](int v) {
            const double s = v / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(GetRValue(c)) + 0.7152 * channel(GetGValue(c)) + 0.0722 * channel(GetBValue(c));
    };
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

}  // namespace

TEST_CASE("OK applies the dialog's fields onto settings that changed while it was open") {
    Settings opened;
    opened.adapterId = L"{a}";
    SettingsDialogState st = StateFromSettings(opened, kChoices);
    st.units = UnitMode::Bits;

    Settings now = opened;  // the report window was moved while the dialog was open
    now.reportRect = RECT{50, 60, 850, 660};
    const Settings out = ApplyDialogState(now, st, kChoices);
    CHECK(out.units == UnitMode::Bits);
    CHECK(out.adapterId == L"{a}");
    REQUIRE(out.reportRect.has_value());
    CHECK(out.reportRect->left == 50);
    CHECK(out.reportRect->bottom == 660);
}

TEST_CASE("Dark settings dialog colours are readable") {
    const ReportPalette p = PaletteFor(ReportTheme::Dark);
    // The source link is text in the accent colour (4.5:1); the system link blue is about 3:1 here.
    CHECK(Contrast(p.accent, p.background) >= 4.5);
    // An unchecked box or radio is a muted outline around a surface fill on the background (UI parts: 3:1).
    CHECK(Contrast(p.muted, p.background) >= 3.0);
    CHECK(Contrast(p.muted, p.surface) >= 3.0);
}

TEST_CASE("StateFromSettings maps every field") {
    Settings s;
    s.startWithWindows = false;
    s.adapterId = L"{b}";
    s.units = UnitMode::Bits;
    s.showGraph = false;
    s.letters = true;
    s.allTaskbars = true;
    const SettingsDialogState st = StateFromSettings(s, kChoices);
    CHECK_FALSE(st.startWithWindows);
    CHECK(st.adapterIndex == 2);  // Automatic is 0, so choices[1] is 2
    CHECK(st.units == UnitMode::Bits);
    CHECK_FALSE(st.showGraph);
    CHECK(st.letters);
    CHECK(st.allTaskbars);
}

TEST_CASE("Automatic is index 0, and an unlisted adapter falls back to it") {
    Settings s;
    CHECK(StateFromSettings(s, kChoices).adapterIndex == 0);
    s.adapterId = L"{gone}";
    CHECK(StateFromSettings(s, kChoices).adapterIndex == 0);
}

TEST_CASE("ApplyDialogState replaces the dialog's fields and keeps the rest") {
    Settings base;
    base.adapterId = L"{a}";
    base.reportRect = RECT{10, 20, 300, 400};
    SettingsDialogState st = StateFromSettings(base, kChoices);
    st.startWithWindows = false;
    st.adapterIndex = 2;
    st.units = UnitMode::Bits;
    st.showGraph = false;
    st.letters = true;
    st.allTaskbars = true;
    const Settings out = ApplyDialogState(base, st, kChoices);
    CHECK_FALSE(out.startWithWindows);
    CHECK(out.adapterId == L"{b}");
    CHECK(out.units == UnitMode::Bits);
    CHECK_FALSE(out.showGraph);
    CHECK(out.letters);
    CHECK(out.allTaskbars);
    REQUIRE(out.reportRect.has_value());
    CHECK(out.reportRect->right == 300);
}

TEST_CASE("Index 0 chooses Automatic; an index past the list keeps the current adapter") {
    Settings base;
    base.adapterId = L"{a}";
    SettingsDialogState st = StateFromSettings(base, kChoices);
    st.adapterIndex = 0;
    CHECK(ApplyDialogState(base, st, kChoices).adapterId.empty());
    st.adapterIndex = 9;
    CHECK(ApplyDialogState(base, st, kChoices).adapterId == L"{a}");
}

TEST_CASE("Opening and pressing OK without changes leaves Settings equal") {
    Settings s;
    s.adapterId = L"{b}";
    s.units = UnitMode::Bits;
    s.letters = true;
    s.reportRect = RECT{1, 2, 3, 4};
    CHECK(ApplyDialogState(s, StateFromSettings(s, kChoices), kChoices) == s);
}

TEST_CASE("A pinned adapter that is hidden or gone is still offered") {
    const auto listed = SettingsAdapterChoices(kChoices, L"{b}");
    CHECK(listed.size() == 2);  // already there: not duplicated

    const auto extra = SettingsAdapterChoices(kChoices, L"{hidden}");
    REQUIRE(extra.size() == 3);
    CHECK(extra[2].first == L"{hidden}");

    Settings s;
    s.adapterId = L"{hidden}";
    CHECK(StateFromSettings(s, extra).adapterIndex == 3);
    CHECK(ApplyDialogState(s, StateFromSettings(s, extra), extra).adapterId == L"{hidden}");

    CHECK(SettingsAdapterChoices(kChoices, L"").size() == 2);  // Automatic adds nothing
}
