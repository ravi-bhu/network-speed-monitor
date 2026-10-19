#include "doctest.h"

#include "ui/dialogs.h"

using namespace nsm;

namespace {

const std::vector<AdapterChoice> kChoices = {{L"{a}", L"Wi-Fi"}, {L"{b}", L"Ethernet"}};

}  // namespace

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
