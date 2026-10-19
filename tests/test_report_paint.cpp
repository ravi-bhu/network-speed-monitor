#include "doctest.h"

#include <windows.h>
#include <d2d1.h>

#include <vector>

#include "ui/report_paint.h"

using namespace nsm;

namespace {

TimelineBar Bar(uint64_t rx, uint64_t tx) {
    TimelineBar b;
    b.totals = Totals{rx, tx};
    return b;
}

}  // namespace

TEST_CASE("ChooseAxis picks a 1-2-2.5-5 step with at most four steps") {
    CHECK(ChooseAxis(0).steps == 0);
    const AxisScale a = ChooseAxis(3'700'000'000);  // 3.7 GB -> 1 GB steps up to 4 GB
    CHECK(a.step == 1'000'000'000);
    CHECK(a.steps == 4);
    CHECK(a.Max() == 4'000'000'000);
    const AxisScale b = ChooseAxis(1000);  // exactly four 250 B steps
    CHECK(b.step == 250);
    CHECK(b.steps == 4);
    const AxisScale c = ChooseAxis(1);
    CHECK(c.step == 1);
    CHECK(c.steps == 1);
    const AxisScale d = ChooseAxis(4'100'000);  // 1.025 MB raw -> 2 MB step, 3 steps (6 MB)
    CHECK(d.step == 2'000'000);
    CHECK(d.steps == 3);
    CHECK(d.Max() >= 4'100'000);
}

TEST_CASE("LayoutBars places bars side by side and stacks sent on received") {
    const D2D1_RECT_F plot{100, 10, 500, 210};  // 400 wide, 200 tall
    const std::vector<TimelineBar> bars{Bar(100, 0), Bar(50, 50), Bar(0, 0), Bar(150, 50)};
    const auto g = LayoutBars(bars, plot, 200);
    REQUIRE(g.size() == 4);
    // Each slot is 100 DIP; the bar is centred in it and narrower than it.
    CHECK(g[0].slotLeft == doctest::Approx(100));
    CHECK(g[0].slotRight == doctest::Approx(200));
    CHECK(g[3].slotRight == doctest::Approx(500));
    CHECK(g[1].left > g[1].slotLeft);
    CHECK(g[1].right < g[1].slotRight);
    CHECK((g[1].left + g[1].right) / 2 == doctest::Approx(250));
    // Heights are proportional to the axis maximum; received sits at the bottom.
    CHECK(g[0].bottom == doctest::Approx(210));
    CHECK(g[0].receivedTop == doctest::Approx(110));  // 100 of 200 -> half the plot
    CHECK(g[0].sentTop == doctest::Approx(110));      // nothing sent
    CHECK(g[1].receivedTop == doctest::Approx(160));
    CHECK(g[1].sentTop == doctest::Approx(110));
    CHECK(g[2].receivedTop == doctest::Approx(210));  // empty bar has no height
    CHECK(g[3].sentTop == doctest::Approx(10));       // full height
}

TEST_CASE("LayoutBars with a zero axis maximum draws nothing tall") {
    const D2D1_RECT_F plot{0, 0, 100, 100};
    const auto g = LayoutBars({Bar(0, 0), Bar(0, 0)}, plot, 0);
    REQUIRE(g.size() == 2);
    CHECK(g[0].sentTop == doctest::Approx(100));
    CHECK(LayoutBars({}, plot, 100).empty());
}

TEST_CASE("HitTestBar finds the slot under the pointer, even above a short bar") {
    const D2D1_RECT_F plot{100, 10, 500, 210};
    const auto g = LayoutBars({Bar(100, 0), Bar(1, 0), Bar(0, 0), Bar(150, 50)}, plot, 200);
    CHECK(HitTestBar(g, plot, 150, 200) == 0);
    CHECK(HitTestBar(g, plot, 250, 20) == 1);   // well above the 1-byte bar: still that slot
    CHECK(HitTestBar(g, plot, 300, 100) == 2);  // slot boundary belongs to the right-hand bar
    CHECK(HitTestBar(g, plot, 499, 100) == 3);
    CHECK(HitTestBar(g, plot, 99, 100) == -1);  // left of the plot (axis labels)
    CHECK(HitTestBar(g, plot, 500, 100) == -1);
    CHECK(HitTestBar(g, plot, 150, 5) == -1);   // above the plot
    CHECK(HitTestBar(g, plot, 150, 215) == -1); // over the axis labels below
    CHECK(HitTestBar({}, plot, 150, 100) == -1);
}

TEST_CASE("LabelStep snaps to calendar-friendly steps per granularity") {
    // Wide slots: every bar gets a label.
    CHECK(LabelStep(Granularity::Hour, 40, 30) == 1);
    CHECK(LabelStep(Granularity::Day, 40, 30) == 1);
    CHECK(LabelStep(Granularity::Month, 40, 30) == 1);
    // 24 hourly bars, 20 DIP each, 45 DIP labels: every 3 hours.
    CHECK(LabelStep(Granularity::Hour, 20, 45) == 3);
    CHECK(LabelStep(Granularity::Hour, 10, 45) == 6);
    CHECK(LabelStep(Granularity::Hour, 3, 45) == 24);
    // Days: 1, 2, then weekly.
    CHECK(LabelStep(Granularity::Day, 20, 30) == 2);
    CHECK(LabelStep(Granularity::Day, 10, 30) == 7);
    CHECK(LabelStep(Granularity::Day, 2, 30) == 28);
    // Months: 1, 2, 3, 6, 12 and then whole multiples of a year.
    CHECK(LabelStep(Granularity::Month, 15, 40) == 3);
    CHECK(LabelStep(Granularity::Month, 2, 40) == 24);
    CHECK(LabelStep(Granularity::Month, 0, 40) >= 1);  // degenerate width never divides by zero
}

TEST_CASE("PaletteFor uses the overlay hues for received and sent") {
    const ReportPalette dark = PaletteFor(ReportTheme::Dark);
    CHECK(dark.received == RGB(0x5E, 0xB8, 0xF0));
    CHECK(dark.sent == RGB(0xF0, 0xA3, 0x5E));
    const ReportPalette light = PaletteFor(ReportTheme::Light);
    CHECK(light.received == RGB(0x00, 0x67, 0xC0));
    CHECK(light.sent == RGB(0xB3, 0x5C, 0x00));
    const ReportPalette hc = PaletteFor(ReportTheme::HighContrast);
    CHECK(hc.background == GetSysColor(COLOR_WINDOW));
    CHECK(hc.text == GetSysColor(COLOR_WINDOWTEXT));
    CHECK(hc.received == GetSysColor(COLOR_HIGHLIGHT));
    // Sent is hollow (window colour inside the outline every High Contrast shape gets), so the two series
    // differ by shape as well as colour whatever the theme's colours are.
    CHECK(hc.sent == GetSysColor(COLOR_WINDOW));
    CHECK(hc.highContrast);
}

namespace {

// A 32bpp DIB with a DC render target bound to it.
struct Canvas {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = nullptr;
    uint32_t* bits = nullptr;
    int w, h;
    ID2D1Factory* factory = nullptr;
    ID2D1DCRenderTarget* rt = nullptr;
    Canvas(int width, int height) : w(width), h(height) {
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&bits), nullptr, 0);
        SelectObject(dc, bmp);
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
        const auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96, 96);
        factory->CreateDCRenderTarget(&props, &rt);
        const RECT r{0, 0, w, h};
        rt->BindDC(dc, &r);
    }
    ~Canvas() {
        if (rt) rt->Release();
        if (factory) factory->Release();
        DeleteDC(dc);
        DeleteObject(bmp);
    }
    COLORREF At(float x, float y) const {
        const uint32_t p = bits[static_cast<int>(y) * w + static_cast<int>(x)];
        return RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
    }
};

}  // namespace

TEST_CASE("PaintTimeline draws received in the download colour under sent in the upload colour" *
          doctest::test_suite("integration")) {
    Canvas c(600, 300);
    REQUIRE(c.rt);
    ReportPainter painter;
    TimelineView v;
    v.granularity = Granularity::Day;
    v.bars = {Bar(3'000'000, 1'000'000), Bar(0, 0), Bar(1'000'000, 1'000'000)};
    v.axisLabels = {L"Mon 13", L"Tue 14", L"Wed 15"};
    v.receivedLegend = L"Received";
    v.sentLegend = L"Sent";
    v.emptyText = L"Nothing";
    const ReportPalette pal = PaletteFor(ReportTheme::Light);

    c.rt->BeginDraw();
    const TimelineLayout layout = painter.PaintTimeline(c.rt, D2D1::SizeF(600, 300), v, pal);
    REQUIRE(SUCCEEDED(c.rt->EndDraw()));
    GdiFlush();

    REQUIRE(layout.bars.size() == 3);
    const BarGeometry& b = layout.bars[0];
    const float x = (b.left + b.right) / 2;
    CHECK(c.At(x, (b.receivedTop + b.bottom) / 2) == pal.received);
    CHECK(c.At(x, (b.sentTop + b.receivedTop) / 2) == pal.sent);
    CHECK(c.At(x, b.sentTop - 6) != pal.sent);  // nothing above the stack
    CHECK(b.sentTop > layout.plot.top - 0.5f);  // 4 MB fits under the axis maximum
    CHECK(c.At(5, 290) == pal.background);
}

TEST_CASE("PaintSummary paints cards and notices without failing" * doctest::test_suite("integration")) {
    Canvas c(800, 500);
    REQUIRE(c.rt);
    ReportPainter painter;
    SummaryView v;
    v.notices = {L"Your usage history couldn't be saved recently."};
    v.cards = {{L"Today", L"1.20 GB", L"↓ 1.00 GB · ↑ 200 MB", false},
               {L"This week", L"3.00 GB", L"↓ 2.00 GB · ↑ 1.00 GB", false},
               {L"This month", L"9.00 GB", L"↓ 8.00 GB · ↑ 1.00 GB", false},
               {L"Last 30 days", L"9.50 GB", L"↓ 8.00 GB · ↑ 1.50 GB", true}};
    v.panels = {{L"Busiest day", {{L"Sunday, October 18, 2026", L"1.20 GB"}}, L"None"},
                {L"Top adapters", {{L"Wi-Fi", L"9.00 GB"}, {L"Ethernet", L"500 MB"}}, L"None"},
                {L"Top Wi-Fi networks", {}, L"No usage recorded in this period."}};
    v.coverage = L"Usage is recorded only while Network Speed Monitor is running.";
    const ReportPalette pal = PaletteFor(ReportTheme::Dark);
    c.rt->BeginDraw();
    painter.PaintSummary(c.rt, D2D1::SizeF(800, 500), v, pal);
    REQUIRE(SUCCEEDED(c.rt->EndDraw()));
    GdiFlush();
    CHECK(c.At(799, 499) == pal.background);
    CHECK(c.At(30, 30) == pal.bannerBackground);  // the notice banner spans the top
    CHECK(painter.SummaryMinHeight(v, 800) > 300);
    CHECK(painter.SummaryMinHeight(v, 800) < 500);
}
