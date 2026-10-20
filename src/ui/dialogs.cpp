#include "ui/dialogs.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <vector>

#include "nsm_version.h"
#include "platform/paths.h"
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

// ---- Settings ----

constexpr wchar_t kButtonClass[] = L"Button";

struct SettingsState {
    Settings settings;
    std::vector<AdapterChoice> choices;
    SettingsDialogState result;  // the controls on OK
    SettingsDialogOptions* options = nullptr;
    DialogTheme theme;
};

bool IsChecked(HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }

SettingsDialogState ReadControls(HWND dlg) {
    SettingsDialogState st;
    st.startWithWindows = IsChecked(dlg, IDC_SET_AUTOSTART);
    const LRESULT sel = SendDlgItemMessageW(dlg, IDC_SET_ADAPTER, CB_GETCURSEL, 0, 0);
    st.adapterIndex = sel < 0 ? 0 : static_cast<size_t>(sel);
    st.units = IsChecked(dlg, IDC_SET_BITS) ? UnitMode::Bits : UnitMode::Bytes;
    st.showGraph = IsChecked(dlg, IDC_SET_GRAPH);
    st.letters = IsChecked(dlg, IDC_SET_LETTERS);
    st.allTaskbars = IsChecked(dlg, IDC_SET_ALL);
    return st;
}

void WriteControls(HWND dlg, const SettingsDialogState& st) {
    CheckDlgButton(dlg, IDC_SET_AUTOSTART, st.startWithWindows ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(dlg, IDC_SET_ADAPTER, CB_SETCURSEL, st.adapterIndex, 0);
    CheckRadioButton(dlg, IDC_SET_BYTES, IDC_SET_BITS, st.units == UnitMode::Bits ? IDC_SET_BITS : IDC_SET_BYTES);
    CheckDlgButton(dlg, IDC_SET_GRAPH, st.showGraph ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dlg, IDC_SET_LETTERS, st.letters ? BST_CHECKED : BST_UNCHECKED);
    CheckRadioButton(dlg, IDC_SET_PRIMARY, IDC_SET_ALL, st.allTaskbars ? IDC_SET_ALL : IDC_SET_PRIMARY);
}

}  // namespace

// Dark check boxes and radio buttons: the themed button ignores WM_CTLCOLORSTATIC and keeps black text,
// so they are drawn here. The report's "Show hidden adapters" box uses this too.
LRESULT DrawDarkToggle(const NMCUSTOMDRAW* cd, const ReportPalette& palette, HBRUSH background) {
    if (cd->dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    const HWND button = cd->hdr.hwndFrom;
    const LONG style = static_cast<LONG>(GetWindowLongPtrW(button, GWL_STYLE)) & BS_TYPEMASK;
    const bool radio = style == BS_AUTORADIOBUTTON || style == BS_RADIOBUTTON;
    const bool check = style == BS_AUTOCHECKBOX || style == BS_CHECKBOX;
    if (!radio && !check) return CDRF_DODEFAULT;  // push buttons are themed dark by DarkMode_Explorer

    const UINT dpi = GetDpiForWindow(button);
    auto scale = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
    HDC dc = cd->hdc;
    const RECT rc = cd->rc;
    FillRect(dc, &rc, background);
    const bool checked = Button_GetCheck(button) == BST_CHECKED;
    // The dark button theme draws an unchecked box as a solid white square, which reads as "checked", so the
    // glyph is drawn here: an outline, filled with the accent colour when set.
    const int size = scale(13);
    RECT boxRect{rc.left, rc.top + (rc.bottom - rc.top - size) / 2, rc.left + size, 0};
    boxRect.bottom = boxRect.top + size;
    const bool hot = (cd->uItemState & (CDIS_HOT | CDIS_SELECTED)) != 0;
    const COLORREF edge = hot ? palette.accent : palette.muted;
    HPEN pen = CreatePen(PS_SOLID, std::max(1, scale(1)), edge);
    HBRUSH fill = CreateSolidBrush(checked && !radio ? palette.accent : palette.surface);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, fill);
    if (radio) {
        Ellipse(dc, boxRect.left, boxRect.top, boxRect.right, boxRect.bottom);
        if (checked) {
            HBRUSH dot = CreateSolidBrush(palette.accent);
            SelectObject(dc, dot);
            SelectObject(dc, GetStockObject(NULL_PEN));
            const int inset = scale(3);
            Ellipse(dc, boxRect.left + inset, boxRect.top + inset, boxRect.right - inset + 1, boxRect.bottom - inset + 1);
            SelectObject(dc, oldBrush);
            DeleteObject(dot);
        }
    } else {
        Rectangle(dc, boxRect.left, boxRect.top, boxRect.right, boxRect.bottom);
        if (checked) {
            HPEN tick = CreatePen(PS_SOLID, std::max(1, scale(2)), RGB(255, 255, 255));
            SelectObject(dc, tick);
            MoveToEx(dc, boxRect.left + scale(3), boxRect.top + scale(7), nullptr);
            LineTo(dc, boxRect.left + scale(5), boxRect.top + scale(9));
            LineTo(dc, boxRect.left + scale(10), boxRect.top + scale(4));
            SelectObject(dc, pen);
            DeleteObject(tick);
        }
    }
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(fill);
    const LRESULT ui = SendMessageW(button, WM_QUERYUISTATE, 0, 0);
    const UINT format = DT_SINGLELINE | DT_VCENTER | DT_LEFT | ((ui & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0u);
    std::wstring label(static_cast<size_t>(GetWindowTextLengthW(button)) + 1, L'\0');
    label.resize(static_cast<size_t>(GetWindowTextW(button, label.data(), static_cast<int>(label.size()))));
    RECT text = rc;
    text.left = boxRect.right + scale(5);
    HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, palette.text);
    DrawTextW(dc, label.c_str(), -1, &text, format);
    if ((cd->uItemState & CDIS_FOCUS) && !(ui & UISF_HIDEFOCUS)) {
        RECT focus = text;
        DrawTextW(dc, label.c_str(), -1, &focus, format | DT_CALCRECT);
        const int h = focus.bottom - focus.top;
        focus.top = text.top + (text.bottom - text.top - h) / 2;
        focus.bottom = focus.top + h;
        InflateRect(&focus, 2, 1);
        DrawFocusRect(dc, &focus);
    }
    SelectObject(dc, old);
    return CDRF_SKIPDEFAULT;
}

namespace {

// Without a source URL the link's row would stay empty above OK/Cancel: remove the link, move the buttons up
// and shrink the dialog by the row's height, keeping it centred.
void RemoveLinkRow(HWND dlg, HWND link) {
    auto clientRect = [dlg](HWND child) {
        RECT r{};
        GetWindowRect(child, &r);
        MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&r), 2);
        return r;
    };
    const int row = clientRect(link).top - clientRect(GetDlgItem(dlg, IDC_SET_ABOUT)).top;
    DestroyWindow(link);
    if (row <= 0) return;
    for (const int id : {IDOK, IDCANCEL}) {
        HWND button = GetDlgItem(dlg, id);
        const RECT r = clientRect(button);
        SetWindowPos(button, nullptr, r.left, r.top - row, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RECT win{};
    GetWindowRect(dlg, &win);
    SetWindowPos(dlg, nullptr, win.left, win.top + row / 2, win.right - win.left, win.bottom - win.top - row,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(dlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        s = reinterpret_cast<SettingsState*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        SetWindowTextW(dlg, LoadStr(IDS_SET_TITLE).c_str());
        const std::pair<int, UINT> texts[] = {
            {IDC_SET_AUTOSTART, IDS_SET_AUTOSTART}, {IDC_SET_ADAPTER_LABEL, IDS_SET_ADAPTER},
            {IDC_SET_UNITS_LABEL, IDS_SET_UNITS},   {IDC_SET_BYTES, IDS_SET_BYTES},
            {IDC_SET_BITS, IDS_SET_BITS},           {IDC_SET_GRAPH, IDS_SET_GRAPH},
            {IDC_SET_LETTERS, IDS_SET_LETTERS},     {IDC_SET_SHOW_ON_LABEL, IDS_SET_SHOW_ON},
            {IDC_SET_PRIMARY, IDS_SET_PRIMARY},     {IDC_SET_ALL, IDS_SET_ALL},
            {IDC_SET_MANAGE, IDS_SET_MANAGE},       {IDC_SET_OPEN_FOLDER, IDS_SET_OPEN_FOLDER},
        };
        for (const auto& [id, str] : texts) SetDlgItemTextW(dlg, id, LoadStr(str).c_str());
        SetButtonTexts(dlg);

        SendDlgItemMessageW(dlg, IDC_SET_ADAPTER, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(LoadStr(IDS_MENU_AUTOMATIC).c_str()));
        for (const auto& c : s->choices)
            SendDlgItemMessageW(dlg, IDC_SET_ADAPTER, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(c.second.c_str()));
        WriteControls(dlg, StateFromSettings(s->settings, s->choices));

        SetDlgItemTextW(dlg, IDC_SET_ABOUT,
                        FillPlaceholders(LoadStr(IDS_SET_ABOUT), {std::wstring(NSM_VERSION_W)}).c_str());
        HWND link = GetDlgItem(dlg, IDC_SET_LINK);
        if (*NSM_SOURCE_URL_W) {
            SetDlgItemTextW(dlg, IDC_SET_LINK, (L"<a>" + LoadStr(IDS_SET_SOURCE) + L"</a>").c_str());
            if (s->theme.dark) {
                // Use the WM_CTLCOLORSTATIC text colour: the system link blue is about 3:1 on the dark dialog.
                LITEM item{};
                item.mask = LIF_ITEMINDEX | LIF_STATE;
                item.state = LIS_DEFAULTCOLORS;
                item.stateMask = LIS_DEFAULTCOLORS;
                SendMessageW(link, LM_SETITEM, 0, reinterpret_cast<LPARAM>(&item));
            }
        } else {
            RemoveLinkRow(dlg, link);
        }
        s->theme.Apply(dlg);
        if (s->theme.dark) SetWindowTheme(GetDlgItem(dlg, IDC_SET_ADAPTER), L"DarkMode_CFD", nullptr);
        return TRUE;  // default focus: the first control
    }
    if (!s) return FALSE;
    if (msg == WM_CTLCOLORSTATIC && reinterpret_cast<HWND>(lp) == GetDlgItem(dlg, IDC_SET_LINK)) {
        const INT_PTR brush = s->theme.CtlColor(wp);
        if (brush) SetTextColor(reinterpret_cast<HDC>(wp), s->theme.palette.accent);
        return brush;
    }
    if (IsCtlColor(msg)) return s->theme.CtlColor(wp);
    if (msg == WM_NOTIFY) {
        const auto* hdr = reinterpret_cast<const NMHDR*>(lp);
        if (hdr->idFrom == IDC_SET_LINK && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            // The app itself never fetches this address: the default browser does.
            ShellExecuteW(dlg, L"open", NSM_SOURCE_URL_W, nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if (hdr->code == NM_CUSTOMDRAW && s->theme.dark && s->theme.brush) {
            wchar_t cls[16] = {};
            GetClassNameW(hdr->hwndFrom, cls, static_cast<int>(std::size(cls)));
            if (_wcsicmp(cls, kButtonClass) == 0) {
                SetWindowLongPtrW(dlg, DWLP_MSGRESULT,
                                  DrawDarkToggle(reinterpret_cast<const NMCUSTOMDRAW*>(lp), s->theme.palette,
                                                 s->theme.brush));
                return TRUE;
            }
        }
        return FALSE;
    }
    if (msg == WM_COMMAND) {
        switch (LOWORD(wp)) {
            case IDC_SET_MANAGE:
                if (s->options) s->options->openAdapters = true;
                [[fallthrough]];  // apply what the controls hold now, then let the caller open the report
            case IDOK:
                s->result = ReadControls(dlg);
                EndDialog(dlg, IDOK);
                return TRUE;
            case IDC_SET_OPEN_FOLDER: {
                const std::filesystem::path dir =
                    s->options && !s->options->dataDir.empty() ? s->options->dataDir : DataDir();
                if (!dir.empty()) ShellExecuteW(dlg, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
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

std::vector<AdapterChoice> SettingsAdapterChoices(const std::vector<AdapterChoice>& adapters,
                                                  const std::wstring& pinnedId) {
    std::vector<AdapterChoice> out = adapters;
    if (!pinnedId.empty() &&
        std::none_of(out.begin(), out.end(), [&](const AdapterChoice& c) { return c.first == pinnedId; })) {
        out.emplace_back(pinnedId, pinnedId);
    }
    return out;
}

SettingsDialogState StateFromSettings(const Settings& s, const std::vector<AdapterChoice>& choices) {
    SettingsDialogState st;
    st.startWithWindows = s.startWithWindows;
    st.adapterIndex = 0;
    if (!s.adapterId.empty()) {
        for (size_t i = 0; i < choices.size(); ++i) {
            if (choices[i].first == s.adapterId) {
                st.adapterIndex = i + 1;
                break;
            }
        }
    }
    st.units = s.units;
    st.showGraph = s.showGraph;
    st.letters = s.letters;
    st.allTaskbars = s.allTaskbars;
    return st;
}

Settings ApplyDialogState(Settings base, const SettingsDialogState& state, const std::vector<AdapterChoice>& choices) {
    base.startWithWindows = state.startWithWindows;
    if (state.adapterIndex == 0) {
        base.adapterId.clear();
    } else if (state.adapterIndex <= choices.size()) {
        base.adapterId = choices[state.adapterIndex - 1].first;
    }
    base.units = state.units;
    base.showGraph = state.showGraph;
    base.letters = state.letters;
    base.allTaskbars = state.allTaskbars;
    return base;
}

std::optional<SettingsDialogState> ShowSettingsDialog(HWND owner, const Settings& s,
                                                      const std::vector<AdapterChoice>& choices,
                                                      SettingsDialogOptions* options) {
    SettingsState state;
    state.settings = s;
    state.choices = choices;
    state.options = options;
    state.theme.dark = options && options->dark;
    if (options) options->openAdapters = false;
    if (DialogBoxParamW(Module(), MAKEINTRESOURCEW(IDD_SETTINGS), owner, &SettingsProc,
                        reinterpret_cast<LPARAM>(&state)) != IDOK)
        return std::nullopt;
    return state.result;
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
