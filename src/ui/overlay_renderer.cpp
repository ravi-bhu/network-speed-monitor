#include "ui/overlay_renderer.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/units.h"
#include "platform/strings.h"
#include "resource.h"

namespace nsm {
namespace {

using Microsoft::WRL::ComPtr;

// Layout in DIPs (1/96 inch).
constexpr float kFontSize = 12.0f;
constexpr float kGraphWidth = 54.0f;
constexpr float kGraphHeight = 28.0f;
constexpr float kGraphGap = 6.0f;
constexpr float kStroke = 1.5f;
constexpr float kTextPad = 1.0f;  // each side of the text block, for glyph overhang
constexpr size_t kGraphSamples = 60;

struct Palette {
    UINT32 text, upload, download, muted;
};
constexpr Palette kDark{0xFFFFFF, 0xF0A35E, 0x5EB8F0, 0x9A9A9A};
constexpr Palette kLight{0x000000, 0xB35C00, 0x0067C0, 0x5F5F5F};

int ToPixels(float dips, unsigned dpi) { return static_cast<int>(std::ceil(dips * static_cast<float>(dpi) / 96.0f - 1e-3f)); }

// Shared DirectWrite objects, created once.
struct Text {
    ComPtr<IDWriteFactory> factory;
    ComPtr<IDWriteTextFormat> format;
    ComPtr<IDWriteTypography> tabular;
    float lineHeight = 0;
    bool ok = false;
};

std::wstring PickFontFamily(IDWriteFactory* factory) {
    ComPtr<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(factory->GetSystemFontCollection(&fonts)) &&
        SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists)
        return L"Segoe UI Variable Text";
    return L"Segoe UI";
}

ComPtr<IDWriteTextLayout> MakeLayout(const Text& t, const std::wstring& s, float maxWidth) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(t.factory->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), t.format.Get(), maxWidth,
                                           t.lineHeight > 0 ? t.lineHeight : 100.0f, &layout)))
        return nullptr;
    layout->SetTypography(t.tabular.Get(), {0, static_cast<UINT32>(s.size())});
    return layout;
}

const Text& GetText() {
    static const Text text = [] {
        Text t;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(t.factory.GetAddressOf()))))
            return t;
        const std::wstring family = PickFontFamily(t.factory.Get());
        if (FAILED(t.factory->CreateTextFormat(family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, kFontSize,
                                               L"en-us", &t.format)) ||
            FAILED(t.factory->CreateTypography(&t.tabular)) ||
            FAILED(t.tabular->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1})))
            return t;
        t.format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        t.format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        t.format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        DWRITE_TEXT_METRICS m{};
        auto probe = MakeLayout(t, L"↑ 888 KB/s", 1000.0f);
        if (!probe || FAILED(probe->GetMetrics(&m))) return t;
        t.lineHeight = m.height;
        t.ok = true;
        return t;
    }();
    return text;
}

const std::wstring& Prefix(bool upload, bool letters) {
    // The STRINGTABLE doesn't change while the app runs, so the labels are loaded once, not every frame.
    static const std::wstring upLetters = LoadStr(IDS_LABEL_UP) + L" ";
    static const std::wstring downLetters = LoadStr(IDS_LABEL_DOWN) + L" ";
    static const std::wstring upArrow = L"↑ ", downArrow = L"↓ ";
    if (letters) return upload ? upLetters : downLetters;
    return upload ? upArrow : downArrow;
}

const std::wstring& NoValue() {
    static const std::wstring text = LoadStr(IDS_NO_VALUE);
    return text;
}

// Makes the brushes on their first frame and recolours them when the taskbar theme changes.
bool PrepareBrushes(ID2D1RenderTarget* rt, OverlayBrushes& b, bool light) {
    const Palette& pal = light ? kLight : kDark;
    if (!b.text || !b.upload || !b.download || !b.muted) {
        b = {};
        if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(pal.text), &b.text)) ||
            FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(pal.upload), &b.upload)) ||
            FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(pal.download), &b.download)) ||
            FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(pal.muted), &b.muted))) {
            b = {};
            return false;
        }
        b.light = light;
    } else if (b.light != light) {
        b.text->SetColor(D2D1::ColorF(pal.text));
        b.upload->SetColor(D2D1::ColorF(pal.upload));
        b.download->SetColor(D2D1::ColorF(pal.download));
        b.muted->SetColor(D2D1::ColorF(pal.muted));
        b.light = light;
    }
    return true;
}

float WidthOf(const Text& t, const std::wstring& s) {
    auto layout = MakeLayout(t, s, 1000.0f);
    DWRITE_TEXT_METRICS m{};
    return layout && SUCCEEDED(layout->GetMetrics(&m)) ? m.widthIncludingTrailingWhitespace : 0.0f;
}

std::wstring FormatValue(double bytesPerSec, UnitMode mode, bool compact) {
    return compact ? FormatSpeedCompact(bytesPerSec, mode) : FormatSpeed(bytesPerSec, mode);
}

float WidestTextWidth(const Text& t, bool letters, bool compact) {
    // Every shape the formatter can print, in both unit modes and at every unit scale:
    // "8.88 X", "88.8 X" and "888 X" (tabular figures, so 8 stands for any digit).
    std::vector<std::wstring> values{NoValue()};
    for (UnitMode mode : {UnitMode::Bytes, UnitMode::Bits}) {
        const double perByte = mode == UnitMode::Bits ? 8.0 : 1.0;
        for (double scale = 1; scale <= 1e9; scale *= 1000) {
            for (double v : {8.88, 88.8, 888.0}) values.push_back(FormatValue(v * scale / perByte, mode, compact));
        }
    }
    float widest = 0;
    for (bool upload : {true, false}) {
        const std::wstring& prefix = Prefix(upload, letters);
        for (const std::wstring& v : values) widest = std::max(widest, WidthOf(t, prefix + v));
    }
    return std::ceil(widest) + 2 * kTextPad;
}

void DrawSeries(ID2D1RenderTarget* rt, const std::vector<double>& values, double max, D2D1_RECT_F box,
                ID2D1Brush* brush) {
    if (values.size() < 2 || max <= 0) return;
    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return;

    const float step = (box.right - box.left) / static_cast<float>(kGraphSamples - 1);
    const float top = box.top + kStroke / 2, bottom = box.bottom - kStroke / 2;
    const size_t n = std::min(values.size(), kGraphSamples);
    const size_t first = values.size() - n;
    for (size_t i = 0; i < n; ++i) {
        const float x = box.right - static_cast<float>(n - 1 - i) * step;
        const float frac = static_cast<float>(std::clamp(values[first + i] / max, 0.0, 1.0));
        const D2D1_POINT_2F pt{x, bottom - frac * (bottom - top)};
        if (i == 0) sink->BeginFigure(pt, D2D1_FIGURE_BEGIN_HOLLOW);
        else sink->AddLine(pt);
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return;
    rt->DrawGeometry(path.Get(), brush, kStroke);
}

}  // namespace

std::wstring LinePrefix(bool upload, bool letters) { return Prefix(upload, letters); }

size_t PrefixColourLength(const std::wstring& prefix) {
    // A translated label may end in its own (no-break) space; only the visible label is coloured.
    const size_t end = prefix.find_last_not_of(L" \t\u00A0\u202F");
    return end == std::wstring::npos ? 0 : end + 1;
}

float LineTopDip(float height, float lineHeight, float scale, int line) {
    const auto snap = [scale](float dip) { return std::round(dip * scale) / scale; };
    // Snap each top on its own: a snapped first top plus a fractional line height falls between pixels.
    const float first = snap((height - 2 * lineHeight) / 2);
    return line == 0 ? first : snap(first + static_cast<float>(line) * lineHeight);
}

float LineWidthDip(const std::wstring& line) {
    const Text& t = GetText();
    return t.ok ? WidthOf(t, line) : 0.0f;
}

OverlaySizes MeasureOverlay(unsigned dpi, bool letters) {
    const Text& t = GetText();
    if (!t.ok) return {0, 0, 0};
    const float text = WidestTextWidth(t, letters, false);
    const float compact = WidestTextWidth(t, letters, true);
    const float height = std::max(2 * t.lineHeight, kGraphHeight);
    return {ToPixels(text + kGraphGap + kGraphWidth, dpi), ToPixels(text, dpi), ToPixels(height, dpi),
            ToPixels(compact, dpi)};
}

void RenderOverlay(ID2D1DCRenderTarget* rt, OverlayBrushes& brushes, SIZE size, const OverlayContent& c) {
    const Text& t = GetText();
    const float dpi = static_cast<float>(c.dpi ? c.dpi : 96);
    const float scale = dpi / 96.0f;
    const float width = static_cast<float>(size.cx) / scale;
    const float height = static_cast<float>(size.cy) / scale;

    rt->SetDpi(dpi, dpi);
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);  // no ClearType on a transparent surface
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 1.0f / 255.0f));  // alpha 1: invisible, but the whole rect takes clicks

    const bool haveBrushes = PrepareBrushes(rt, brushes, c.light);

    float textLeft = 0;
    if (haveBrushes && c.showGraph && c.history) {
        const float top = (height - kGraphHeight) / 2;
        const D2D1_RECT_F box{0, top, kGraphWidth, top + kGraphHeight};
        const double max = c.history->Max();
        DrawSeries(rt, c.history->Rx(), max, box, brushes.download.Get());
        DrawSeries(rt, c.history->Tx(), max, box, brushes.upload.Get());
        textLeft = kGraphWidth + kGraphGap;
    }

    if (t.ok && haveBrushes) {
        const float textWidth = std::max(0.0f, width - textLeft - 2 * kTextPad);
        for (bool upload : {true, false}) {
            const std::wstring value = c.speed ? FormatValue(upload ? c.speed->tx : c.speed->rx, c.units, c.compact)
                                               : NoValue();
            const std::wstring& prefix = Prefix(upload, c.letters);
            auto layout = MakeLayout(t, prefix + value, textWidth);
            if (!layout) continue;
            ID2D1Brush* series = upload ? brushes.upload.Get() : brushes.download.Get();
            // Arrow in the series colour; "U:"/"D:" in the muted colour (the separator needs no colour).
            layout->SetDrawingEffect(c.letters ? brushes.muted.Get() : series,
                                     {0, static_cast<UINT32>(PrefixColourLength(prefix))});
            // Each line on a whole device pixel so text stays crisp.
            const float y = LineTopDip(height, t.lineHeight, scale, upload ? 0 : 1);
            rt->DrawTextLayout({textLeft + kTextPad, y}, layout.Get(), brushes.text.Get(),
                               D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
    }

    // A software DC target never loses its device, so there is nothing to recreate on failure.
    rt->EndDraw();
}

}  // namespace nsm
