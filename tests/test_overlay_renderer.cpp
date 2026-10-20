#include <windows.h>
#include <d2d1.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/speed_history.h"
#include "core/units.h"
#include "doctest.h"
#include "platform/strings.h"
#include "resource.h"
#include "ui/overlay_renderer.h"

using namespace nsm;

TEST_CASE("MeasureOverlay: graph adds width, sizes are positive" * doctest::test_suite("integration")) {
    for (bool letters : {false, true}) {
        const OverlaySizes s = MeasureOverlay(96, letters);
        CHECK(s.textOnlyWidth > 0);
        CHECK(s.fullWidth > s.textOnlyWidth);
        CHECK(s.fullWidth - s.textOnlyWidth >= 60);  // 54 DIP graph + 6 DIP gap at 96 DPI
        CHECK(s.height >= 28);                        // at least the graph height
        CHECK(s.height <= 40);                        // fits a 100% Win10 taskbar
    }
}

TEST_CASE("MeasureOverlay leaves room for every formatted value" * doctest::test_suite("integration")) {
    // The text block keeps 1 DIP of padding on each side; at 96 DPI one px == one DIP.
    for (bool letters : {false, true}) {
        const int room = MeasureOverlay(96, letters).textOnlyWidth - 2;
        for (bool upload : {true, false}) {
            const std::wstring prefix = LinePrefix(upload, letters);
            CAPTURE(letters);
            CAPTURE(upload);
            CHECK(LineWidthDip(prefix + FormatSpeed(11.1e6, UnitMode::Bits)) <= room);  // "88.8 Mbps"
            for (UnitMode mode : {UnitMode::Bytes, UnitMode::Bits}) {
                const double perByte = mode == UnitMode::Bits ? 8.0 : 1.0;
                for (double scale = 1; scale <= 1e9; scale *= 1000) {
                    for (double v : {8.88, 88.8, 888.0, 999.0}) {
                        CAPTURE(v);
                        CAPTURE(scale);
                        CHECK(LineWidthDip(prefix + FormatSpeed(v * scale / perByte, mode)) <= room);
                    }
                }
            }
        }
    }
}

TEST_CASE("Letter labels are loaded from the STRINGTABLE" * doctest::test_suite("integration")) {
    CHECK(LinePrefix(true, true) == LoadStr(IDS_LABEL_UP) + L" ");
    CHECK(LinePrefix(false, true) == LoadStr(IDS_LABEL_DOWN) + L" ");
    CHECK(LinePrefix(true, false) == L"↑ ");
    CHECK(LinePrefix(false, false) == L"↓ ");
}

TEST_CASE("MeasureOverlay scales with DPI" * doctest::test_suite("integration")) {
    const OverlaySizes a = MeasureOverlay(96, false);
    const OverlaySizes b = MeasureOverlay(144, false);
    const OverlaySizes c = MeasureOverlay(192, false);
    CHECK(std::abs(b.fullWidth - a.fullWidth * 1.5) <= 2);
    CHECK(std::abs(b.textOnlyWidth - a.textOnlyWidth * 1.5) <= 2);
    CHECK(std::abs(b.height - a.height * 1.5) <= 2);
    CHECK(std::abs(c.fullWidth - a.fullWidth * 2) <= 2);
    CHECK(std::abs(c.height - a.height * 2) <= 2);
}

namespace {

struct Dib {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    uint32_t* bits = nullptr;
    Dib(int w, int h) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        void* p = nullptr;
        bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
        bits = static_cast<uint32_t*>(p);
        old = SelectObject(dc, bmp);
    }
    ~Dib() {
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
    }
};

}  // namespace

TEST_CASE("RenderOverlay draws a clickable background, text and graph" * doctest::test_suite("integration")) {
    const OverlaySizes s = MeasureOverlay(96, false);
    const SIZE size{s.fullWidth, s.height};
    Dib dib(size.cx, size.cy);
    REQUIRE(dib.bits != nullptr);

    ID2D1Factory* factory = nullptr;
    REQUIRE(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory)));
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ID2D1DCRenderTarget* rt = nullptr;
    REQUIRE(SUCCEEDED(factory->CreateDCRenderTarget(&props, &rt)));
    const RECT rc{0, 0, size.cx, size.cy};
    REQUIRE(SUCCEEDED(rt->BindDC(dib.dc, &rc)));

    SpeedHistory history;
    for (int i = 0; i < 60; ++i) history.Push({1e6 * (1 + std::sin(i * 0.3)), 2e5 * (1 + std::cos(i * 0.2))});
    const OverlayContent c{Speed{1.24e6, 15.8e6}, UnitMode::Bytes, false, true, &history, false, 96};
    OverlayBrushes brushes;
    RenderOverlay(rt, brushes, size, c);
    GdiFlush();

    int transparent = 0, opaque = 0, colouredGraph = 0;
    const int graphRight = 54;  // the graph occupies the left 54 px at 96 DPI
    for (int y = 0; y < size.cy; ++y) {
        for (int x = 0; x < size.cx; ++x) {
            const uint32_t px = dib.bits[y * size.cx + x];
            const unsigned a = px >> 24;
            if (a == 0) ++transparent;
            if (a >= 200) {
                ++opaque;
                const unsigned r = (px >> 16) & 0xFF, b = px & 0xFF;
                if (x < graphRight && r != b) ++colouredGraph;  // series colours are not grey
            }
        }
    }
    CHECK(transparent == 0);    // alpha >= 1 everywhere, so the whole rect takes clicks
    CHECK(opaque > 50);         // text was drawn
    CHECK(colouredGraph > 10);  // graph polylines were drawn in series colours

    rt->Release();
    factory->Release();
}

TEST_CASE("MeasureOverlay: compact width fits a default-width vertical taskbar (#30)" * doctest::test_suite("integration")) {
    for (bool letters : {false, true}) {
        const OverlaySizes s = MeasureOverlay(96, letters);
        CHECK(s.compactWidth > 0);
        CHECK(s.compactWidth < s.textOnlyWidth);
        CHECK(s.compactWidth <= 62);  // a 70 DIP left/right taskbar minus 2x4 DIP margins
        for (UnitMode mode : {UnitMode::Bytes, UnitMode::Bits})
            for (double v : {8.88e6, 88.8e6, 888e6, 9'996.0, 99'960.0})
                CHECK(LineWidthDip(LinePrefix(true, letters) + FormatSpeedCompact(v, mode)) <= s.compactWidth - 2);
    }
}

TEST_CASE("PrefixColourLength colours the label, not the separator after it (#7)") {
    CHECK(PrefixColourLength(L"↑ ") == 1);
    CHECK(PrefixColourLength(L"U: ") == 2);
    CHECK(PrefixColourLength(L"Up: ") == 3);       // a longer localized label
    CHECK(PrefixColourLength(L"Hoch:") == 5);      // no separator at all
    CHECK(PrefixColourLength(L"U:  ") == 2);       // a label that brings its own space
    CHECK(PrefixColourLength(L"U :\u00A0") == 3);  // inner space kept, trailing no-break space dropped
    CHECK(PrefixColourLength(L" ") == 0);          // a missing label
    CHECK(PrefixColourLength(L"") == 0);
}

TEST_CASE("Line prefixes colour exactly their label (#7)" * doctest::test_suite("integration")) {
    CHECK(PrefixColourLength(LinePrefix(true, true)) == LoadStr(IDS_LABEL_UP).size());
    CHECK(PrefixColourLength(LinePrefix(false, true)) == LoadStr(IDS_LABEL_DOWN).size());
    CHECK(PrefixColourLength(LinePrefix(true, false)) == 1);
    CHECK(PrefixColourLength(LinePrefix(false, false)) == 1);
}

TEST_CASE("LineTopDip puts both readout lines on whole device pixels (#7)") {
    for (float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
        for (float lineHeight : {15.96f, 16.0f, 16.6f}) {
            for (float height : {std::ceil(2 * lineHeight * scale) / scale, 40.0f}) {
                CAPTURE(scale);
                CAPTURE(lineHeight);
                CAPTURE(height);
                const float top = LineTopDip(height, lineHeight, scale, 0);
                const float bottom = LineTopDip(height, lineHeight, scale, 1);
                for (float y : {top, bottom}) CHECK(std::abs(y * scale - std::round(y * scale)) < 1e-3f);
                // Each line moves less than half a device pixel from its exact place.
                CHECK(std::abs(top - (height - 2 * lineHeight) / 2) * scale <= 0.5f + 1e-3f);
                CHECK(std::abs(bottom - (top + lineHeight)) * scale <= 0.5f + 1e-3f);
            }
        }
    }
}

namespace {

struct Palette {
    uint32_t text, upload, download, muted;
};
constexpr Palette kDarkColours{0xFFFFFF, 0xF0A35E, 0x5EB8F0, 0x9A9A9A};
constexpr Palette kLightColours{0x000000, 0xB35C00, 0x0067C0, 0x5F5F5F};

// One rendered frame at 96 DPI, where one pixel is one DIP.
struct Frame {
    int width = 0, height = 0;
    std::vector<uint32_t> px;

    // The un-premultiplied RGB of a well-covered pixel; nullopt for the background and soft glyph edges.
    std::optional<uint32_t> Colour(int x, int y) const {
        const uint32_t p = px[static_cast<size_t>(y) * width + x];
        const unsigned a = p >> 24;
        if (a < 192) return std::nullopt;
        const auto un = [a](unsigned c) { return std::min(255u, (c * 255 + a / 2) / a); };
        return un((p >> 16) & 0xFF) << 16 | un((p >> 8) & 0xFF) << 8 | un(p & 0xFF);
    }
};

bool Near(uint32_t a, uint32_t b) {
    for (int shift : {0, 8, 16}) {
        const int d = static_cast<int>((a >> shift) & 0xFF) - static_cast<int>((b >> shift) & 0xFF);
        if (std::abs(d) > 24) return false;
    }
    return true;
}

// The pixels of one colour in rows [top, bottom): how many, and the columns they span.
struct Ink {
    int count = 0, left = INT_MAX, right = -1;
    int Width() const { return count ? right - left + 1 : 0; }
};

Ink InkOf(const Frame& f, uint32_t colour, int top, int bottom) {
    Ink ink;
    for (int y = top; y < bottom; ++y) {
        for (int x = 0; x < f.width; ++x) {
            const std::optional<uint32_t> c = f.Colour(x, y);
            if (!c || !Near(*c, colour)) continue;
            ++ink.count;
            ink.left = std::min(ink.left, x);
            ink.right = std::max(ink.right, x);
        }
    }
    return ink;
}
Ink UploadLine(const Frame& f, uint32_t colour) { return InkOf(f, colour, 0, f.height / 2); }
Ink DownloadLine(const Frame& f, uint32_t colour) { return InkOf(f, colour, f.height / 2, f.height); }
Ink Anywhere(const Frame& f, uint32_t colour) { return InkOf(f, colour, 0, f.height); }

struct TestTarget {
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> rt;

    TestTarget() {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return;
        const auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        factory->CreateDCRenderTarget(&props, &rt);
    }

    Frame Render(SIZE size, const OverlayContent& c, OverlayBrushes& brushes) {
        Frame f{size.cx, size.cy, {}};
        Dib dib(size.cx, size.cy);
        const RECT rc{0, 0, size.cx, size.cy};
        if (!rt || !dib.bits || FAILED(rt->BindDC(dib.dc, &rc))) return f;
        RenderOverlay(rt.Get(), brushes, size, c);
        GdiFlush();
        f.px.assign(dib.bits, dib.bits + static_cast<size_t>(size.cx) * size.cy);
        return f;
    }
    Frame Render(SIZE size, const OverlayContent& c) {
        OverlayBrushes brushes;
        return Render(size, c, brushes);
    }
};

// Upload 15.8 MB/s on the top line, download 1.24 MB/s below, no graph.
OverlayContent Readout(bool letters, bool light) {
    return OverlayContent{Speed{1.24e6, 15.8e6}, UnitMode::Bytes, letters, false, nullptr, light, 96};
}

}  // namespace

TEST_CASE("RenderOverlay colours labels and values in both palettes (#7)" * doctest::test_suite("integration")) {
    TestTarget target;
    REQUIRE(target.rt.Get() != nullptr);
    for (bool light : {false, true}) {
        for (bool letters : {false, true}) {
            CAPTURE(light);
            CAPTURE(letters);
            const OverlaySizes s = MeasureOverlay(96, letters);
            const Frame f = target.Render({s.textOnlyWidth, s.height}, Readout(letters, light));
            REQUIRE(!f.px.empty());
            const Palette& pal = light ? kLightColours : kDarkColours;
            const Palette& other = light ? kDarkColours : kLightColours;

            // The values are in the text colour on both lines.
            const Ink upValue = UploadLine(f, pal.text), downValue = DownloadLine(f, pal.text);
            CHECK(upValue.count > 20);
            CHECK(downValue.count > 20);
            // Arrows take their series colour; U:/D: are muted and never take a series colour.
            const Ink upLabel = UploadLine(f, letters ? pal.muted : pal.upload);
            const Ink downLabel = DownloadLine(f, letters ? pal.muted : pal.download);
            CHECK(upLabel.count > 0);
            CHECK(downLabel.count > 0);
            // The colouring covers the label and stops there.
            const std::wstring upText = letters ? LoadStr(IDS_LABEL_UP) : L"↑";
            const std::wstring downText = letters ? LoadStr(IDS_LABEL_DOWN) : L"↓";
            CHECK(upLabel.Width() <= std::ceil(LineWidthDip(upText)) + 2);
            CHECK(downLabel.Width() <= std::ceil(LineWidthDip(downText)) + 2);
            CHECK(upLabel.right < upValue.left);
            CHECK(downLabel.right < downValue.left);
            CHECK(UploadLine(f, pal.download).count == 0);
            CHECK(DownloadLine(f, pal.upload).count == 0);
            if (letters) {
                CHECK(Anywhere(f, pal.upload).count == 0);
                CHECK(Anywhere(f, pal.download).count == 0);
            } else {
                CHECK(Anywhere(f, pal.muted).count == 0);
            }
            // Nothing from the other palette.
            CHECK(Anywhere(f, other.text).count == 0);
            CHECK(Anywhere(f, other.upload).count == 0);
            CHECK(Anywhere(f, other.download).count == 0);
        }
    }
}

TEST_CASE("RenderOverlay shows the placeholder when there is no speed (#7)" * doctest::test_suite("integration")) {
    TestTarget target;
    REQUIRE(target.rt.Get() != nullptr);
    const OverlaySizes s = MeasureOverlay(96, false);
    OverlayContent c = Readout(false, false);
    c.speed = std::nullopt;
    const Frame f = target.Render({s.textOnlyWidth, s.height}, c);
    REQUIRE(!f.px.empty());
    const Frame withSpeed = target.Render({s.textOnlyWidth, s.height}, Readout(false, false));
    REQUIRE(!withSpeed.px.empty());

    const float dash = LineWidthDip(LoadStr(IDS_NO_VALUE));
    for (const Ink& value : {UploadLine(f, kDarkColours.text), DownloadLine(f, kDarkColours.text)}) {
        CHECK(value.count > 0);
        CHECK(value.Width() <= std::ceil(dash) + 2);
    }
    CHECK(UploadLine(withSpeed, kDarkColours.text).Width() > UploadLine(f, kDarkColours.text).Width());
    CHECK(UploadLine(f, kDarkColours.upload).count > 0);  // the arrows stay
    CHECK(DownloadLine(f, kDarkColours.download).count > 0);
}

TEST_CASE("RenderOverlay in text-only mode leaves the graph area empty (#7)" * doctest::test_suite("integration")) {
    TestTarget target;
    REQUIRE(target.rt.Get() != nullptr);
    SpeedHistory history;
    for (int i = 0; i < 60; ++i) history.Push({1e6 * (1 + std::sin(i * 0.3)), 2e5 * (1 + std::cos(i * 0.2))});
    const OverlaySizes s = MeasureOverlay(96, false);
    OverlayContent c = Readout(false, false);
    c.history = &history;  // a history, but the placement turned the graph off

    // In the full-width window the readout keeps to the right: the graph's columns stay blank.
    const Frame wide = target.Render({s.fullWidth, s.height}, c);
    REQUIRE(!wide.px.empty());
    int drawnLeft = 0;  // pixels with more alpha than the clear colour
    for (int y = 0; y < wide.height; ++y) {
        for (int x = 0; x < wide.width - s.textOnlyWidth; ++x) {
            if ((wide.px[static_cast<size_t>(y) * wide.width + x] >> 24) > 1) ++drawnLeft;
        }
    }
    CHECK(drawnLeft == 0);
    CHECK(UploadLine(wide, kDarkColours.text).count > 20);

    // In a text-only window the whole readout fits, arrows included.
    const Frame narrow = target.Render({s.textOnlyWidth, s.height}, c);
    REQUIRE(!narrow.px.empty());
    CHECK(UploadLine(narrow, kDarkColours.upload).count > 0);
    CHECK(DownloadLine(narrow, kDarkColours.download).count > 0);
    CHECK(UploadLine(narrow, kDarkColours.text).count > 20);
}

TEST_CASE("RenderOverlay draws the compact form in the compact width (#7)" * doctest::test_suite("integration")) {
    TestTarget target;
    REQUIRE(target.rt.Get() != nullptr);
    for (bool letters : {false, true}) {
        CAPTURE(letters);
        const OverlaySizes s = MeasureOverlay(96, letters);
        OverlayContent c = Readout(letters, false);
        c.compact = true;
        const Frame f = target.Render({s.compactWidth, s.height}, c);
        REQUIRE(!f.px.empty());

        // "↑ 15.8MB" over "↓ 1.24MB": the label is not clipped and the value is the compact one.
        const Ink upLabel = UploadLine(f, letters ? kDarkColours.muted : kDarkColours.upload);
        const Ink downLabel = DownloadLine(f, letters ? kDarkColours.muted : kDarkColours.download);
        const Ink upValue = UploadLine(f, kDarkColours.text), downValue = DownloadLine(f, kDarkColours.text);
        CHECK(upLabel.count > 0);
        CHECK(downLabel.count > 0);
        CHECK(upLabel.right < upValue.left);
        CHECK(downLabel.right < downValue.left);
        CHECK(upValue.Width() <= std::ceil(LineWidthDip(FormatSpeedCompact(15.8e6, UnitMode::Bytes))) + 2);
        CHECK(downValue.Width() <= std::ceil(LineWidthDip(FormatSpeedCompact(1.24e6, UnitMode::Bytes))) + 2);
        CHECK(upValue.Width() < std::floor(LineWidthDip(FormatSpeed(15.8e6, UnitMode::Bytes))) - 4);
    }
}

TEST_CASE("RenderOverlay reuses its brushes and recolours them (#7)" * doctest::test_suite("integration")) {
    TestTarget target;
    REQUIRE(target.rt.Get() != nullptr);
    const OverlaySizes s = MeasureOverlay(96, false);
    const SIZE size{s.textOnlyWidth, s.height};
    OverlayBrushes brushes;

    const Frame dark = target.Render(size, Readout(false, false), brushes);
    REQUIRE(brushes.text.Get() != nullptr);
    ID2D1SolidColorBrush* const text = brushes.text.Get();
    ID2D1SolidColorBrush* const upload = brushes.upload.Get();
    CHECK(Anywhere(dark, kDarkColours.text).count > 20);

    const Frame light = target.Render(size, Readout(false, true), brushes);
    CHECK(brushes.text.Get() == text);  // recoloured, not recreated
    CHECK(brushes.upload.Get() == upload);
    CHECK(brushes.light);
    CHECK(Anywhere(light, kLightColours.text).count > 20);
    CHECK(Anywhere(light, kDarkColours.text).count == 0);
    CHECK(UploadLine(light, kLightColours.upload).count > 0);

    const Frame again = target.Render(size, Readout(false, false), brushes);
    CHECK(brushes.text.Get() == text);
    CHECK(Anywhere(again, kDarkColours.text).count > 20);
    CHECK(Anywhere(again, kLightColours.text).count == 0);
}
