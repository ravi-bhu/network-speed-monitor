#include <windows.h>
#include <d2d1.h>

#include <cmath>
#include <cstdint>

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
    RenderOverlay(rt, size, c);
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
