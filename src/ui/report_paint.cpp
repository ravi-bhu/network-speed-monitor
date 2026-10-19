#include "ui/report_paint.h"

#include <algorithm>
#include <cmath>

#include "core/units.h"

namespace nsm {
namespace {

using Microsoft::WRL::ComPtr;

// Layout in DIPs.
constexpr float kPad = 12.0f;        // inside cards, panels and banners
constexpr float kGap = 12.0f;        // between cards and panels
constexpr float kSectionGap = 16.0f;  // between rows of the summary
constexpr float kRowHeight = 22.0f;  // a panel row
constexpr int kPanelRows = 3;        // panels are sized for the top three
constexpr float kBarFill = 0.7f;     // bar width as a share of its slot

constexpr COLORREF Hex(uint32_t rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }

D2D1_COLOR_F ToColor(COLORREF c) {
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, 1.0f);
}

struct Brushes {
    ComPtr<ID2D1SolidColorBrush> background, surface, border, text, muted, received, sent, grid, bannerBg, bannerText,
        accent;
    Brushes(ID2D1RenderTarget* rt, const ReportPalette& p) {
        rt->CreateSolidColorBrush(ToColor(p.background), &background);
        rt->CreateSolidColorBrush(ToColor(p.surface), &surface);
        rt->CreateSolidColorBrush(ToColor(p.border), &border);
        rt->CreateSolidColorBrush(ToColor(p.text), &text);
        rt->CreateSolidColorBrush(ToColor(p.muted), &muted);
        rt->CreateSolidColorBrush(ToColor(p.received), &received);
        rt->CreateSolidColorBrush(ToColor(p.sent), &sent);
        rt->CreateSolidColorBrush(ToColor(p.grid), &grid);
        rt->CreateSolidColorBrush(ToColor(p.bannerBackground), &bannerBg);
        rt->CreateSolidColorBrush(ToColor(p.bannerText), &bannerText);
        rt->CreateSolidColorBrush(ToColor(p.accent), &accent);
    }
    bool Ok() const {
        return background && surface && border && text && muted && received && sent && grid && bannerBg &&
               bannerText && accent;
    }
};

void Text(ID2D1RenderTarget* rt, IDWriteTextFormat* f, const std::wstring& s, D2D1_RECT_F r, ID2D1Brush* b) {
    if (!f || s.empty() || r.right <= r.left) return;
    rt->DrawTextW(s.c_str(), static_cast<UINT32>(s.size()), f, r, b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float LineHeight(IDWriteTextFormat* f) { return f ? std::ceil(f->GetFontSize() * 1.36f) : 16.0f; }

ComPtr<IDWriteTextFormat> MakeFormat(IDWriteFactory* dw, float size, DWRITE_FONT_WEIGHT weight,
                                     DWRITE_TEXT_ALIGNMENT align, bool wrap) {
    ComPtr<IDWriteTextFormat> f;
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = L"en-us";
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    if (!dw || FAILED(dw->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, size, locale, &f)))
        return nullptr;
    f->SetTextAlignment(align);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (!wrap) {
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dw->CreateEllipsisTrimmingSign(f.Get(), &ellipsis))) {
            const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            f->SetTrimming(&trimming, ellipsis.Get());
        }
    }
    return f;
}

}  // namespace

ReportPalette PaletteFor(ReportTheme theme) {
    switch (theme) {
        case ReportTheme::HighContrast: {
            // Sent is hollow: the window colour inside the outline every chart shape gets in High Contrast, so
            // the two series differ by shape and not only by colour.
            const COLORREF window = GetSysColor(COLOR_WINDOW), text = GetSysColor(COLOR_WINDOWTEXT);
            return {window, window, text, text, text, GetSysColor(COLOR_HIGHLIGHT), window,
                    GetSysColor(COLOR_GRAYTEXT), GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT),
                    GetSysColor(COLOR_HIGHLIGHT), true};
        }
        case ReportTheme::Dark:
            return {Hex(0x202020), Hex(0x2B2B2B), Hex(0x3D3D3D), Hex(0xFFFFFF), Hex(0xA0A0A0), Hex(0x5EB8F0),
                    Hex(0xF0A35E), Hex(0x353535), Hex(0x4A3B12), Hex(0xFFFFFF), Hex(0x5EB8F0)};
        case ReportTheme::Light:
        default:
            return {Hex(0xFFFFFF), Hex(0xF5F5F5), Hex(0xE0E0E0), Hex(0x1A1A1A), Hex(0x5F5F5F), Hex(0x0067C0),
                    Hex(0xB35C00), Hex(0xE6E6E6), Hex(0xFFF4CE), Hex(0x1A1A1A), Hex(0x0067C0)};
    }
}

// ---- Geometry ----

AxisScale ChooseAxis(uint64_t maxValue, int maxSteps) {
    if (maxValue == 0 || maxSteps <= 0) return {};
    const double raw = static_cast<double>(maxValue) / maxSteps;
    // Smallest 1/2/2.5/5 x 10^k step (whole bytes) that covers maxValue in at most maxSteps steps.
    uint64_t step = 1;
    for (uint64_t decade = 1;; decade *= 10) {
        bool found = false;
        for (const double m : {1.0, 2.0, 2.5, 5.0}) {
            const double candidate = m * static_cast<double>(decade);
            if (candidate >= raw && candidate == std::floor(candidate)) {
                step = static_cast<uint64_t>(candidate);
                found = true;
                break;
            }
        }
        if (found || decade > UINT64_MAX / 100) break;
    }
    const uint64_t steps = (maxValue + step - 1) / step;
    return {step, static_cast<int>(std::max<uint64_t>(steps, 1))};
}

std::vector<BarGeometry> LayoutBars(const std::vector<TimelineBar>& bars, D2D1_RECT_F plot, uint64_t axisMax) {
    std::vector<BarGeometry> out;
    if (bars.empty()) return out;
    out.reserve(bars.size());
    const float width = plot.right - plot.left;
    const float height = plot.bottom - plot.top;
    const float slot = width / static_cast<float>(bars.size());
    const float barWidth = slot < 3.0f ? slot : std::max(1.0f, slot * kBarFill);
    auto heightOf = [&](uint64_t v) {
        if (axisMax == 0) return 0.0f;
        return static_cast<float>(std::min(1.0, static_cast<double>(v) / static_cast<double>(axisMax))) * height;
    };
    for (size_t i = 0; i < bars.size(); ++i) {
        BarGeometry g;
        g.slotLeft = plot.left + slot * static_cast<float>(i);
        g.slotRight = i + 1 == bars.size() ? plot.right : plot.left + slot * static_cast<float>(i + 1);
        const float centre = (g.slotLeft + g.slotRight) / 2;
        g.left = centre - barWidth / 2;
        g.right = centre + barWidth / 2;
        g.bottom = plot.bottom;
        g.receivedTop = plot.bottom - heightOf(bars[i].totals.rx);
        g.sentTop = plot.bottom - heightOf(bars[i].totals.Total());
        out.push_back(g);
    }
    return out;
}

int HitTestBar(const std::vector<BarGeometry>& bars, D2D1_RECT_F plot, float x, float y) {
    if (bars.empty() || y < plot.top || y > plot.bottom || x < plot.left || x >= plot.right) return -1;
    for (size_t i = 0; i < bars.size(); ++i) {
        if (x >= bars[i].slotLeft && x < bars[i].slotRight) return static_cast<int>(i);
    }
    return -1;
}

size_t LabelStep(Granularity g, float slotWidth, float labelWidth) {
    static constexpr size_t kHours[] = {1, 2, 3, 4, 6, 12, 24};
    static constexpr size_t kDays[] = {1, 2, 7, 14, 28};
    static constexpr size_t kMonths[] = {1, 2, 3, 6, 12, 24};
    const size_t* steps = g == Granularity::Hour ? kHours : g == Granularity::Day ? kDays : kMonths;
    const size_t count = g == Granularity::Hour ? std::size(kHours) : g == Granularity::Day ? std::size(kDays)
                                                                                           : std::size(kMonths);
    if (!(slotWidth > 0)) return steps[count - 1];
    for (size_t i = 0; i < count; ++i) {
        if (static_cast<float>(steps[i]) * slotWidth >= labelWidth) return steps[i];
    }
    size_t step = steps[count - 1];
    while (static_cast<float>(step) * slotWidth < labelWidth && step < (SIZE_MAX / 2)) step *= 2;
    return step;
}

// ---- Painter ----

ReportPainter::ReportPainter() {
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return;
    IDWriteFactory* dw = dwrite_.Get();
    line_ = MakeFormat(dw, 12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    lineRight_ = MakeFormat(dw, 12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, false);
    lineCenter_ = MakeFormat(dw, 12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    wrap_ = MakeFormat(dw, 12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    title_ = MakeFormat(dw, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    value_ = MakeFormat(dw, 22, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
}

float ReportPainter::TextHeight(IDWriteTextFormat* f, const std::wstring& s, float width) {
    if (!f || !dwrite_) return 16.0f;
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS m{};
    if (FAILED(dwrite_->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), f, std::max(1.0f, width), 10000.0f,
                                         &layout)) ||
        FAILED(layout->GetMetrics(&m)))
        return LineHeight(f);
    return std::ceil(m.height);
}

float ReportPainter::TextWidth(IDWriteTextFormat* f, const std::wstring& s) {
    if (!f || !dwrite_) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS m{};
    if (FAILED(dwrite_->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), f, 10000.0f, 1000.0f, &layout)) ||
        FAILED(layout->GetMetrics(&m)))
        return 0.0f;
    return std::ceil(m.widthIncludingTrailingWhitespace);
}

float ReportPainter::CardHeight() const {
    return kPad + LineHeight(line_.Get()) + 4 + LineHeight(value_.Get()) + 4 + LineHeight(line_.Get()) + kPad;
}

float ReportPainter::PanelHeight() const {
    return kPad + LineHeight(title_.Get()) + 6 + kRowHeight * kPanelRows + kPad - 4;
}

float ReportPainter::SummaryMinHeight(const SummaryView& v, float width) {
    float y = 0;
    for (const auto& n : v.notices) y += TextHeight(wrap_.Get(), n, width - 2 * kPad) + 2 * 10.0f + kGap;
    if (!v.cards.empty()) y += CardHeight() + kSectionGap;
    if (!v.panels.empty()) y += PanelHeight() + kSectionGap;
    y += TextHeight(wrap_.Get(), v.coverage, width);
    return std::ceil(y);
}

void ReportPainter::PaintSummary(ID2D1RenderTarget* rt, D2D1_SIZE_F size, const SummaryView& v,
                                 const ReportPalette& p) {
    rt->Clear(ToColor(p.background));
    const Brushes b(rt, p);
    if (!b.Ok()) return;
    const float width = size.width;
    float y = 0;

    for (const auto& notice : v.notices) {
        const float h = TextHeight(wrap_.Get(), notice, width - 2 * kPad) + 2 * 10.0f;
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, y, width, y + h), 4, 4), b.bannerBg.Get());
        Text(rt, wrap_.Get(), notice, D2D1::RectF(kPad, y + 10, width - kPad, y + h), b.bannerText.Get());
        y += h + kGap;
    }

    if (!v.cards.empty()) {
        const float n = static_cast<float>(v.cards.size());
        const float cardW = (width - kGap * (n - 1)) / n;
        const float h = CardHeight();
        for (size_t i = 0; i < v.cards.size(); ++i) {
            const SummaryCard& c = v.cards[i];
            const float x = (cardW + kGap) * static_cast<float>(i);
            const D2D1_RECT_F box = D2D1::RectF(x, y, x + cardW, y + h);
            rt->FillRoundedRectangle(D2D1::RoundedRect(box, 6, 6), b.surface.Get());
            const float stroke = c.highlight ? 2.0f : 1.0f;
            const D2D1_RECT_F edge = D2D1::RectF(box.left + stroke / 2, box.top + stroke / 2, box.right - stroke / 2,
                                                 box.bottom - stroke / 2);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(edge, 6, 6), c.highlight ? b.accent.Get() : b.border.Get(),
                                     stroke);
            float ty = y + kPad;
            Text(rt, line_.Get(), c.title, D2D1::RectF(x + kPad, ty, x + cardW - kPad, ty + LineHeight(line_.Get())),
                 b.muted.Get());
            ty += LineHeight(line_.Get()) + 4;
            Text(rt, value_.Get(), c.value, D2D1::RectF(x + kPad, ty, x + cardW - kPad, ty + LineHeight(value_.Get())),
                 b.text.Get());
            ty += LineHeight(value_.Get()) + 4;
            Text(rt, line_.Get(), c.detail, D2D1::RectF(x + kPad, ty, x + cardW - kPad, ty + LineHeight(line_.Get())),
                 b.muted.Get());
        }
        y += h + kSectionGap;
    }

    if (!v.panels.empty()) {
        const float n = static_cast<float>(v.panels.size());
        const float panelW = (width - kGap * (n - 1)) / n;
        const float h = PanelHeight();
        for (size_t i = 0; i < v.panels.size(); ++i) {
            const SummaryPanel& panel = v.panels[i];
            const float x = (panelW + kGap) * static_cast<float>(i);
            const D2D1_RECT_F box = D2D1::RectF(x, y, x + panelW, y + h);
            rt->FillRoundedRectangle(D2D1::RoundedRect(box, 6, 6), b.surface.Get());
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(box.left + 0.5f, box.top + 0.5f, box.right - 0.5f, box.bottom - 0.5f), 6,
                                  6),
                b.border.Get(), 1.0f);
            float ty = y + kPad;
            Text(rt, title_.Get(), panel.title,
                 D2D1::RectF(x + kPad, ty, x + panelW - kPad, ty + LineHeight(title_.Get())), b.text.Get());
            ty += LineHeight(title_.Get()) + 6;
            if (panel.rows.empty()) {
                Text(rt, line_.Get(), panel.emptyText, D2D1::RectF(x + kPad, ty, x + panelW - kPad, ty + kRowHeight),
                     b.muted.Get());
            }
            for (size_t r = 0; r < panel.rows.size() && r < kPanelRows; ++r) {
                const auto& [label, value] = panel.rows[r];
                const float valueW = value.empty() ? 0.0f : TextWidth(lineRight_.Get(), value) + 8;
                Text(rt, line_.Get(), label,
                     D2D1::RectF(x + kPad, ty, x + panelW - kPad - valueW, ty + kRowHeight),
                     r == 0 || !value.empty() ? b.text.Get() : b.muted.Get());
                Text(rt, lineRight_.Get(), value, D2D1::RectF(x + kPad, ty, x + panelW - kPad, ty + kRowHeight),
                     b.text.Get());
                ty += kRowHeight;
            }
        }
        y += h + kSectionGap;
    }

    Text(rt, wrap_.Get(), v.coverage, D2D1::RectF(0, y, width, size.height), b.muted.Get());
}

TimelineLayout ReportPainter::PaintTimeline(ID2D1RenderTarget* rt, D2D1_SIZE_F size, const TimelineView& v,
                                            const ReportPalette& p) {
    TimelineLayout out;
    rt->Clear(ToColor(p.background));
    const Brushes b(rt, p);
    if (!b.Ok()) return out;
    const bool highContrast = p.highContrast;
    const float lineH = LineHeight(line_.Get());

    // Legend: a swatch and a label per series.
    float x = 0;
    const float swatch = 10.0f;
    const float swatchTop = (lineH - swatch) / 2;
    const std::pair<std::wstring, ID2D1Brush*> legend[] = {{v.receivedLegend, b.received.Get()},
                                                           {v.sentLegend, b.sent.Get()}};
    for (const auto& [label, brush] : legend) {
        const D2D1_RECT_F sw = D2D1::RectF(x, swatchTop, x + swatch, swatchTop + swatch);
        rt->FillRectangle(sw, brush);
        if (highContrast) rt->DrawRectangle(sw, b.text.Get(), 1.0f);
        x += swatch + 6;
        const float w = TextWidth(line_.Get(), label);
        Text(rt, line_.Get(), label, D2D1::RectF(x, 0, x + w + 2, lineH), b.text.Get());
        x += w + 20;
    }

    uint64_t maxTotal = 0;
    for (const auto& bar : v.bars) maxTotal = std::max(maxTotal, bar.totals.Total());
    const AxisScale axis = ChooseAxis(maxTotal);
    if (v.bars.empty() || axis.steps == 0) {
        Text(rt, lineCenter_.Get(), v.emptyText,
             D2D1::RectF(0, size.height / 2 - lineH / 2, size.width, size.height / 2 + lineH), b.muted.Get());
        return out;
    }

    // Y axis labels decide the plot's left edge.
    std::vector<std::wstring> yLabels;
    float yLabelW = 0;
    for (int k = 0; k <= axis.steps; ++k) {
        yLabels.push_back(FormatVolume(axis.step * static_cast<uint64_t>(k)));
        yLabelW = std::max(yLabelW, TextWidth(lineRight_.Get(), yLabels.back()));
    }
    const D2D1_RECT_F plot =
        D2D1::RectF(yLabelW + 8, lineH + 16, std::max(yLabelW + 9, size.width - 4), size.height - lineH - 6);
    if (plot.bottom - plot.top < 10) return out;
    out.plot = plot;

    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (int k = 0; k <= axis.steps; ++k) {
        const float gy = std::round(plot.bottom - (plot.bottom - plot.top) * static_cast<float>(k) / axis.steps);
        rt->DrawLine(D2D1::Point2F(plot.left, gy + 0.5f), D2D1::Point2F(plot.right, gy + 0.5f),
                     k == 0 ? b.border.Get() : b.grid.Get(), 1.0f);
        Text(rt, lineRight_.Get(), yLabels[static_cast<size_t>(k)],
             D2D1::RectF(0, gy - lineH / 2, yLabelW, gy + lineH / 2),
             b.muted.Get());
    }

    out.bars = LayoutBars(v.bars, plot, axis.Max());
    for (size_t i = 0; i < out.bars.size(); ++i) {
        const BarGeometry& g = out.bars[i];
        if (static_cast<int>(i) == v.hot) {
            rt->FillRectangle(D2D1::RectF(g.slotLeft, plot.top, g.slotRight, plot.bottom), b.surface.Get());
        }
        const D2D1_RECT_F rx = D2D1::RectF(g.left, g.receivedTop, g.right, g.bottom);
        const D2D1_RECT_F tx = D2D1::RectF(g.left, g.sentTop, g.right, g.receivedTop);
        if (g.bottom - g.receivedTop > 0.01f) {
            rt->FillRectangle(rx, b.received.Get());
            if (highContrast) rt->DrawRectangle(rx, b.text.Get(), 1.0f);
        }
        if (g.receivedTop - g.sentTop > 0.01f) {
            rt->FillRectangle(tx, b.sent.Get());
            if (highContrast) rt->DrawRectangle(tx, b.text.Get(), 1.0f);
        }
    }

    if (v.focused) {
        D2D1_RECT_F cue = plot;
        if (v.hot >= 0 && static_cast<size_t>(v.hot) < out.bars.size()) {
            cue = D2D1::RectF(out.bars[static_cast<size_t>(v.hot)].slotLeft, plot.top,
                              out.bars[static_cast<size_t>(v.hot)].slotRight, plot.bottom);
        }
        ComPtr<ID2D1Factory> factory;
        rt->GetFactory(&factory);
        ComPtr<ID2D1StrokeStyle> dashed;
        factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                               D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10.0f,
                                                               D2D1_DASH_STYLE_DOT),
                                   nullptr, 0, &dashed);
        rt->DrawRectangle(D2D1::RectF(cue.left + 0.5f, cue.top + 0.5f, cue.right - 0.5f, cue.bottom - 0.5f),
                          b.text.Get(), 1.0f, dashed.Get());
    }
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // X axis labels, thinned so they never collide.
    float labelW = 0;
    for (const auto& l : v.axisLabels) labelW = std::max(labelW, TextWidth(lineCenter_.Get(), l));
    labelW += 8;
    const float slot = (plot.right - plot.left) / static_cast<float>(out.bars.size());
    const size_t step = LabelStep(v.granularity, slot, labelW);
    for (size_t i = 0; i < out.bars.size() && i < v.axisLabels.size(); i += step) {
        const float c = (out.bars[i].slotLeft + out.bars[i].slotRight) / 2;
        const float left = std::clamp(c - labelW / 2, 0.0f, std::max(0.0f, size.width - labelW));
        Text(rt, lineCenter_.Get(), v.axisLabels[i],
             D2D1::RectF(left, plot.bottom + 4, left + labelW, plot.bottom + 4 + lineH),
             b.muted.Get());
    }
    return out;
}

}  // namespace nsm
