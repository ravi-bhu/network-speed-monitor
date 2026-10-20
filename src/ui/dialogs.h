#pragma once
#include <windows.h>
#include <commctrl.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/types.h"
#include "platform/settings.h"

namespace nsm {

struct ReportPalette;

// Modal dialogs of the report window. `dark` gives them a dark title bar and dark surfaces.

// The new friendly name (trimmed; empty = use the Windows name), or nullopt if cancelled.
std::optional<std::wstring> RenameDialog(HWND owner, const std::wstring& current, const std::wstring& windowsName,
                                         bool dark);
// An inclusive local date range (from <= to), or nullopt if cancelled.
std::optional<std::pair<LocalDate, LocalDate>> CustomRangeDialog(HWND owner, LocalDate from, LocalDate to, bool dark);
// A "Delete"/"Cancel" task dialog with Cancel as the default. True if the user chose Delete.
bool ConfirmDelete(HWND owner, const std::wstring& message);
void ShowError(HWND owner, const std::wstring& message);

// Dark check boxes and radio buttons, drawn from a Button's NM_CUSTOMDRAW on `background` (the Settings dialog
// and the report's "Show hidden adapters" box). Push buttons and later draw stages get CDRF_DODEFAULT.
LRESULT DrawDarkToggle(const NMCUSTOMDRAW* cd, const ReportPalette& palette, HBRUSH background);

std::wstring TrimName(const std::wstring& name);

// ---- Settings dialog (opened from the overlay's right-click menu) ----

using AdapterChoice = std::pair<std::wstring, std::wstring>;  // id, display name

// What the dialog's controls hold. adapterIndex 0 = Automatic, i + 1 = choices[i].
struct SettingsDialogState {
    bool startWithWindows = true;
    size_t adapterIndex = 0;
    UnitMode units = UnitMode::Bytes;
    bool showGraph = true;
    bool letters = false;
    bool allTaskbars = false;

    friend bool operator==(const SettingsDialogState&, const SettingsDialogState&) = default;
};

// The adapter list as the combo box shows it: `adapters` plus the pinned adapter if it is not among them
// (hidden or unplugged), so the pinned choice is never silently dropped.
std::vector<AdapterChoice> SettingsAdapterChoices(const std::vector<AdapterChoice>& adapters,
                                                  const std::wstring& pinnedId);
SettingsDialogState StateFromSettings(const Settings& s, const std::vector<AdapterChoice>& choices);
// `base` with the dialog's fields replaced; everything else (report window rectangle) is kept. An
// adapterIndex outside the list keeps base.adapterId.
Settings ApplyDialogState(Settings base, const SettingsDialogState& state, const std::vector<AdapterChoice>& choices);

struct SettingsDialogOptions {
    bool dark = false;
    std::filesystem::path dataDir;  // what "Open data folder" opens; empty = the default data folder
    // Out: "Manage adapters…" was pressed. The dialog then closes as OK with the controls' current state
    // applied, so nothing the user changed is lost; the caller opens the report on the Adapters tab.
    bool openAdapters = false;
};

// Modal; the controls start from `s`, and `choices` (from SettingsAdapterChoices) fills the adapter list. Returns
// what the controls hold on OK, or nullopt for Cancel and Esc. Settings can change while the dialog is open (the
// report window's rectangle), so apply the result with ApplyDialogState onto the settings as they are then.
std::optional<SettingsDialogState> ShowSettingsDialog(HWND owner, const Settings& s,
                                                      const std::vector<AdapterChoice>& choices,
                                                      SettingsDialogOptions* options = nullptr);

}  // namespace nsm
