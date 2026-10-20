#pragma once
#include <windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/periods.h"
#include "core/time_zone.h"
#include "report/report_model.h"
#include "storage/usage_store.h"
#include "ui/report_format.h"
#include "ui/report_paint.h"

namespace nsm {

// The usage report (spec §9): a period picker, Export CSV…, Clear all history…, and the tabs Summary,
// Timeline, Adapters and Wi-Fi networks. Closing destroys the window to keep idle memory low; Show
// creates it again. UI thread only.
class ReportWindow {
public:
    // owner: the app's hidden window. An owned report is never the window a polite close of the process
    // (taskkill without /f) is sent to, so that still reaches the app and ends it.
    ReportWindow(HINSTANCE inst, UsageStore& store, HWND owner = nullptr);
    ~ReportWindow();
    ReportWindow(const ReportWindow&) = delete;
    ReportWindow& operator=(const ReportWindow&) = delete;

    void Show(int tab);  // create or bring to front; tab 0..3 = Summary, Timeline, Adapters, Wi-Fi networks
    void Refresh();
    bool IsOpen() const { return hwnd_ != nullptr; }
    void Close();  // destroy the window if open (app exit)
    // Call from the message loop before TranslateMessage: Ctrl+Tab, Tab order, mnemonics, Esc.
    bool PreTranslateMessage(MSG& msg);

    void SetCorruptionNotice(std::filesystem::path recoveredFile);
    // Where the next Show creates the window (its restored bounds), and whether it opens maximized.
    void SetSavedRect(std::optional<RECT> rect, bool maximized = false);
    void SetThemeOverride(std::optional<ReportTheme> theme);  // development runs only

    std::function<void()> beforeQuery;          // App::FlushNow
    // The window was moved, resized or closed maximized: its restored bounds (screen px) and whether it is
    // maximized (or minimized from maximized).
    std::function<void(RECT, bool)> onBoundsChanged;
    std::function<void()> onAdaptersChanged;    // a rename or hide/unhide was stored
    // Tests only: stand-ins for a table row's context menu (returns the chosen item's id, 0 = none) and for the
    // Rename dialog (returns the new name, nullopt = cancelled), which can't be shown in a test run.
    std::function<UINT(HMENU)> pickRowCommand;
    std::function<std::optional<std::wstring>(const std::wstring& current)> askNewName;

private:
    struct TableState;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK TabsSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK ListSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleCanvas(HWND canvas, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleNotify(NMHDR* nm);

    bool Create();
    void CreateControls();
    void ApplyFont();
    void ApplyTheme();
    void Layout();
    void SelectTab(int tab);
    void ReportBounds();
    void OnDestroy();
    int Scale(int dips) const;

    void OnPeriodPicked();
    UtcRange CurrentRange(int64_t nowUtc, const TimeZone& tz, int firstDayOfWeek) const;
    std::wstring SelectedId(const TableState& t) const;
    void FillTable(TableState& t, const std::wstring& selectedId);
    void ShowRowMenu(HWND list, int item, POINT screenPt);
    void ExportCsvFile();
    void ConfirmClearAll();
    void AdaptersChanged();

    void PaintCanvas(HWND canvas, HDC dc);
    void PaintTabs(HWND tabs);
    void SetHot(int index, bool fromKeyboard);
    std::wstring BarTip(int index) const;
    ReportTheme CurrentTheme() const;

    HINSTANCE inst_;
    UsageStore& store_;
    HWND owner_;
    HWND hwnd_ = nullptr;
    HWND periodLabel_ = nullptr, period_ = nullptr, range_ = nullptr, export_ = nullptr, clearAll_ = nullptr;
    HWND tabs_ = nullptr, summary_ = nullptr, timeline_ = nullptr, tooltip_ = nullptr;
    HWND adaptersNote_ = nullptr, showHiddenBox_ = nullptr, wifiNote_ = nullptr;
    HWND lastFocus_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;
    UINT dpi_ = 96;
    int tab_ = 0;
    int pageTop_ = 0, pageWidth_ = 0;  // client px, from the last Layout

    ReportTheme theme_ = ReportTheme::Light;
    ReportPalette palette_{};
    std::optional<ReportTheme> themeOverride_;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;  // shared by both canvases, bound per paint
    std::unique_ptr<ReportPainter> painter_;

    int periodIndex_;  // into the Period list; kept across close and reopen
    LocalDate customFrom_{}, customTo_{};
    bool periodPending_ = false;

    SummaryView summaryView_;
    TimelineView timelineView_;
    TimelineLayout timelineLayout_;
    bool trackingMouse_ = false;
    std::wstring tipText_;
    std::unique_ptr<TableState> adapters_, wifi_;
    bool showHiddenRows_ = false;

    std::optional<RECT> savedRect_;
    bool savedMaximized_ = false;
    bool repaintAfterRecreate_ = false;  // a canvas lost its render target and asked for one more paint
    std::filesystem::path corruptFile_;
};

// ---- Pure rules (exposed for tests) ----

// Where the report window opens: the saved rect moved inside `work` (shrunk to fit, grown to
// `minSize` when smaller), or `defaultSize` centred in `work` when nothing was saved. Screen pixels.
RECT FitReportRect(const std::optional<RECT>& saved, const RECT& work, SIZE defaultSize, SIZE minSize);
// GetWindowPlacement's rcNormalPosition is in workspace coordinates, whose (0, 0) is the corner of the work
// area of the window's monitor: shifted from screen coordinates when a taskbar is on the left or top edge.
RECT WorkspaceToScreen(RECT r, const RECT& monitor, const RECT& work);
// WM_CONTEXTMENU from the keyboard (Shift+F10 or the menu key) has the point (-1, -1). Not lParam == -1: on
// 64-bit Windows the lParam is 0xFFFFFFFF.
bool IsKeyboardContextMenu(LPARAM lp);

// The Summary always shows cards for Today, This week and This month (0, 1, 2). When the picked period is one
// of them its card is highlighted instead of adding a period card that repeats it; otherwise -1.
int FixedSummaryCard(Period p);

// The Summary tab's banners: the database was unreadable and moved to `recoveredFile` (empty = it wasn't), and
// the store failed a read or write since it last succeeded (!storeHealthy).
std::vector<std::wstring> SummaryNotices(const std::filesystem::path& recoveredFile, bool storeHealthy);

}  // namespace nsm
