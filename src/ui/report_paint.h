#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "report/report_model.h"

namespace nsm {

// Colours of the report window (COLORREF, 0x00BBGGRR). Received and sent use the overlay's hues.
struct ReportPalette {
    COLORREF background, surface, border, text, muted, received, sent, grid, bannerBackground, bannerText, accent;
    bool highContrast = false;  // outline chart shapes so they don't rely on colour alone
};
enum class ReportTheme { Light, Dark, HighContrast };
ReportPalette PaletteFor(ReportTheme theme);  // HighContrast uses GetSysColor only

// ---- Timeline geometry, in DIPs ----

// Y axis: `steps` gridline intervals of `step` bytes; step is 1, 2, 2.5 or 5 x 10^k. steps == 0 when max is 0.
struct AxisScale {
    uint64_t step = 0;
    int steps = 0;
    uint64_t Max() const { return step * static_cast<uint64_t>(steps); }
};
AxisScale ChooseAxis(uint64_t maxValue, int maxSteps = 4);

struct BarGeometry {
    float slotLeft, slotRight;  // the bar's share of the plot width (hover target)
    float left, right;          // the drawn bar
    float bottom, receivedTop, sentTop;  // received from bottom to receivedTop, sent from there to sentTop
};
std::vector<BarGeometry> LayoutBars(const std::vector<TimelineBar>& bars, D2D1_RECT_F plot, uint64_t axisMax);
// The bar whose slot holds (x, y) inside the plot, or -1.
int HitTestBar(const std::vector<BarGeometry>& bars, D2D1_RECT_F plot, float x, float y);
// Label every Nth bar so labels `labelWidth` wide don't collide; N snaps to 1/2/3/4/6/12/24 hours,
// 1/2/7/14/28 days or 1/2/3/6/12/24 months, doubling beyond that.
size_t LabelStep(Granularity g, float slotWidth, float labelWidth);

// ---- What the canvases show (all text already formatted and localized) ----

struct SummaryCard {
    std::wstring title, value, detail;
    bool highlight = false;  // the selected period's card
};
struct SummaryPanel {
    std::wstring title;
    std::vector<std::pair<std::wstring, std::wstring>> rows;  // label, value
    std::wstring emptyText;
};
struct SummaryView {
    std::vector<std::wstring> notices;  // warning banners, top first
    std::vector<SummaryCard> cards;
    std::vector<SummaryPanel> panels;
    std::wstring coverage;
};

struct TimelineView {
    std::vector<TimelineBar> bars;
    Granularity granularity = Granularity::Day;
    std::vector<std::wstring> axisLabels;  // one per bar
    std::wstring receivedLegend, sentLegend, emptyText;
    int hot = -1;          // highlighted bar (hover or keyboard)
    bool focused = false;  // draw a focus cue around the hot bar
};
struct TimelineLayout {
    D2D1_RECT_F plot{};
    std::vector<BarGeometry> bars;
};

// Draws the Summary and Timeline tabs with Direct2D/DirectWrite. Holds only device-independent
// DirectWrite objects; the caller owns the render target and calls BeginDraw/EndDraw.
class ReportPainter {
public:
    ReportPainter();
    ReportPainter(const ReportPainter&) = delete;
    ReportPainter& operator=(const ReportPainter&) = delete;

    void PaintSummary(ID2D1RenderTarget* rt, D2D1_SIZE_F size, const SummaryView& v, const ReportPalette& p);
    TimelineLayout PaintTimeline(ID2D1RenderTarget* rt, D2D1_SIZE_F size, const TimelineView& v,
                                 const ReportPalette& p);
    // Height in DIPs the summary needs at `width` DIPs, so the window can set its minimum size.
    float SummaryMinHeight(const SummaryView& v, float width);

private:
    float TextHeight(IDWriteTextFormat* f, const std::wstring& s, float width);
    float TextWidth(IDWriteTextFormat* f, const std::wstring& s);
    float CardHeight() const;
    float PanelHeight() const;

    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> line_, lineRight_, lineCenter_, wrap_, title_, value_;
};

}  // namespace nsm
