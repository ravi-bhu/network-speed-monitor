#include "ui/dialogs.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <cwctype>

#include "platform/strings.h"
#include "resource.h"
#include "ui/report_paint.h"
#include "ui/report_window.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace nsm {
namespace {

constexpr DWORD kDwmUseImmersiveDarkMode = 20;  // DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 20H1+)
constexpr int kDeleteButton = 100;

HINSTANCE Module() { return reinterpret_cast<HINSTANCE>(&__ImageBase); }

// Dark title bar, dark buttons and edits, dark surfaces. Date pickers stay light (no dark theme exists).
struct DialogTheme {
    bool dark = false;
    HBRUSH brush = nullptr;
    ReportPalette palette = PaletteFor(ReportTheme::Dark);

    void Apply(HWND dlg) {
        if (!dark) return;
        BOOL on = TRUE;
        DwmSetWindowAttribute(dlg, kDwmUseImmersiveDarkMode, &on, sizeof(on));
        brush = CreateSolidBrush(palette.background);
        EnumChildWindows(
            dlg,
            [](HWND child, LPARAM) -> BOOL {
                wchar_t cls[32] = {};
                GetClassNameW(child, cls, static_cast<int>(std::size(cls)));
                if (_wcsicmp(cls, WC_BUTTONW) == 0) SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
                if (_wcsicmp(cls, WC_EDITW) == 0) SetWindowTheme(child, L"DarkMode_CFD", nullptr);
                return TRUE;
            },
            0);
    }
    // WM_CTLCOLOR* handling; 0 = not handled.
    INT_PTR CtlColor(WPARAM wp) const {
        if (!dark || !brush) return 0;
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, palette.text);
        SetBkColor(dc, palette.background);
        return reinterpret_cast<INT_PTR>(brush);
    }
    ~DialogTheme() {
        if (brush) DeleteObject(brush);
    }
};

bool IsCtlColor(UINT msg) {
    return msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORBTN;
}

std::wstring EscapeAmpersands(const std::wstring& s) {
    std::wstring out;
    for (const wchar_t c : s) {
        out += c;
        if (c == L'&') out += L'&';
    }
    return out;
}

void SetButtonTexts(HWND dlg) {
    SetDlgItemTextW(dlg, IDOK, LoadStr(IDS_BTN_OK).c_str());
    SetDlgItemTextW(dlg, IDCANCEL, LoadStr(IDS_BTN_CANCEL).c_str());
}

// ---- Rename ----

struct RenameState {
    std::wstring current, windowsName, result;
    DialogTheme theme;
};

INT_PTR CALLBACK RenameProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<RenameState*>(GetWindowLongPtrW(dlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        s = reinterpret_cast<RenameState*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        SetWindowTextW(dlg, LoadStr(IDS_RENAME_TITLE).c_str());
        SetDlgItemTextW(dlg, IDC_RENAME_PROMPT,
                        FillPlaceholders(LoadStr(IDS_RENAME_PROMPT), {EscapeAmpersands(s->windowsName)}).c_str());
        SetButtonTexts(dlg);
        HWND edit = GetDlgItem(dlg, IDC_RENAME_EDIT);
        SendMessageW(edit, EM_LIMITTEXT, 256, 0);
        SetWindowTextW(edit, s->current.c_str());
        s->theme.Apply(dlg);
        SetFocus(edit);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return FALSE;  // focus set here
    }
    if (!s) return FALSE;
    if (IsCtlColor(msg)) return s->theme.CtlColor(wp);
    if (msg == WM_COMMAND) {
        switch (LOWORD(wp)) {
            case IDOK: {
                HWND edit = GetDlgItem(dlg, IDC_RENAME_EDIT);
                std::wstring text(static_cast<size_t>(GetWindowTextLengthW(edit)) + 1, L'\0');
                text.resize(static_cast<size_t>(GetWindowTextW(edit, text.data(), static_cast<int>(text.size()))));
                s->result = TrimName(text);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            case IDCANCEL:
                EndDialog(dlg, IDCANCEL);
                return TRUE;
        }
    }
    return FALSE;
}

// ---- Custom range ----

struct RangeState {
    LocalDate from, to;
    DialogTheme theme;
};

SYSTEMTIME ToSystemTime(LocalDate d) {
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(d.year);
    st.wMonth = static_cast<WORD>(d.month);
    st.wDay = static_cast<WORD>(d.day);
    return st;
}

LocalDate PickerDate(HWND picker) {
    SYSTEMTIME st{};
    DateTime_GetSystemtime(picker, &st);
    return LocalDate{st.wYear, st.wMonth, st.wDay};
}

INT_PTR CALLBACK RangeProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<RangeState*>(GetWindowLongPtrW(dlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        s = reinterpret_cast<RangeState*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        SetWindowTextW(dlg, LoadStr(IDS_RANGE_TITLE).c_str());
        SetDlgItemTextW(dlg, IDC_RANGE_FROM_LABEL, LoadStr(IDS_RANGE_FROM).c_str());
        SetDlgItemTextW(dlg, IDC_RANGE_TO_LABEL, LoadStr(IDS_RANGE_TO).c_str());
        SetButtonTexts(dlg);
        SYSTEMTIME from = ToSystemTime(s->from), to = ToSystemTime(s->to);
        DateTime_SetSystemtime(GetDlgItem(dlg, IDC_RANGE_FROM), GDT_VALID, &from);
        DateTime_SetSystemtime(GetDlgItem(dlg, IDC_RANGE_TO), GDT_VALID, &to);
        s->theme.Apply(dlg);
        return TRUE;  // default focus: the first control
    }
    if (!s) return FALSE;
    if (IsCtlColor(msg)) return s->theme.CtlColor(wp);
    if (msg == WM_COMMAND) {
        switch (LOWORD(wp)) {
            case IDOK:
                s->from = PickerDate(GetDlgItem(dlg, IDC_RANGE_FROM));
                s->to = PickerDate(GetDlgItem(dlg, IDC_RANGE_TO));
                if (s->to < s->from) std::swap(s->from, s->to);
                EndDialog(dlg, IDOK);
                return TRUE;
            case IDCANCEL:
                EndDialog(dlg, IDCANCEL);
                return TRUE;
        }
    }
    return FALSE;
}

}  // namespace

std::wstring TrimName(const std::wstring& name) {
    size_t first = 0, last = name.size();
    while (first < last && std::iswspace(name[first])) ++first;
    while (last > first && std::iswspace(name[last - 1])) --last;
    return name.substr(first, last - first);
}

std::optional<std::wstring> RenameDialog(HWND owner, const std::wstring& current, const std::wstring& windowsName,
                                         bool dark) {
    RenameState s;
    s.current = current;
    s.windowsName = windowsName;
    s.theme.dark = dark;
    if (DialogBoxParamW(Module(), MAKEINTRESOURCEW(IDD_RENAME), owner, &RenameProc, reinterpret_cast<LPARAM>(&s)) !=
        IDOK)
        return std::nullopt;
    return s.result;
}

std::optional<std::pair<LocalDate, LocalDate>> CustomRangeDialog(HWND owner, LocalDate from, LocalDate to, bool dark) {
    RangeState s;
    s.from = from;
    s.to = to;
    s.theme.dark = dark;
    if (DialogBoxParamW(Module(), MAKEINTRESOURCEW(IDD_RANGE), owner, &RangeProc, reinterpret_cast<LPARAM>(&s)) != IDOK)
        return std::nullopt;
    return std::make_pair(s.from, s.to);
}

bool ConfirmDelete(HWND owner, const std::wstring& message) {
    const std::wstring title = LoadStr(IDS_APP_NAME), del = LoadStr(IDS_BTN_DELETE), cancel = LoadStr(IDS_BTN_CANCEL);
    // TaskDialogIndirect exists only in Common Controls 6, which the app's manifest selects. Resolving it at
    // run time keeps it out of the import table of binaries without that manifest (the test runner).
    using TaskDialogIndirectFn = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    HMODULE comctl = GetModuleHandleW(L"comctl32.dll");
    const auto taskDialog =
        comctl ? reinterpret_cast<TaskDialogIndirectFn>(GetProcAddress(comctl, "TaskDialogIndirect")) : nullptr;
    if (!taskDialog) {
        return MessageBoxW(owner, message.c_str(), title.c_str(), MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK;
    }
    const TASKDIALOG_BUTTON buttons[] = {{kDeleteButton, del.c_str()}, {IDCANCEL, cancel.c_str()}};
    TASKDIALOGCONFIG config{sizeof(config)};
    config.hwndParent = owner;
    config.hInstance = Module();
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = title.c_str();
    config.pszMainIcon = TD_WARNING_ICON;
    config.pszContent = message.c_str();
    config.cButtons = static_cast<UINT>(std::size(buttons));
    config.pButtons = buttons;
    config.nDefaultButton = IDCANCEL;
    int pressed = IDCANCEL;
    if (FAILED(taskDialog(&config, &pressed, nullptr, nullptr))) return false;
    return pressed == kDeleteButton;
}

void ShowError(HWND owner, const std::wstring& message) {
    MessageBoxW(owner, message.c_str(), LoadStr(IDS_APP_NAME).c_str(), MB_OK | MB_ICONERROR);
}

}  // namespace nsm
