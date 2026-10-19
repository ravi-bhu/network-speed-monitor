#pragma once
#include <windows.h>

#include <optional>
#include <string>
#include <utility>

#include "core/types.h"

namespace nsm {

// Modal dialogs of the report window. `dark` gives them a dark title bar and dark surfaces.

// The new friendly name (trimmed; empty = use the Windows name), or nullopt if cancelled.
std::optional<std::wstring> RenameDialog(HWND owner, const std::wstring& current, const std::wstring& windowsName,
                                         bool dark);
// An inclusive local date range (from <= to), or nullopt if cancelled.
std::optional<std::pair<LocalDate, LocalDate>> CustomRangeDialog(HWND owner, LocalDate from, LocalDate to, bool dark);
// A "Delete"/"Cancel" task dialog with Cancel as the default. True if the user chose Delete.
bool ConfirmDelete(HWND owner, const std::wstring& message);
void ShowError(HWND owner, const std::wstring& message);

std::wstring TrimName(const std::wstring& name);

}  // namespace nsm
