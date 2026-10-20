#pragma once
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <optional>
#include <string>

#include "core/overlay_layout.h"
#include "core/speed_history.h"
#include "core/types.h"

namespace nsm {

struct OverlayContent {
    std::optional<Speed> speed;  // nullopt -> "—"
    UnitMode units;
    bool letters;                // U:/D: instead of arrows
    bool showGraph;
    const SpeedHistory* history;
    bool light;                  // light taskbar
    unsigned dpi;
    bool compact = false;        // FormatSpeedCompact values (narrow or crowded taskbar)
};

// The brushes RenderOverlay draws with. They belong to one render target: keep one of these next to it,
// created empty; the first frame fills it and later frames only recolour it when the palette changes.
struct OverlayBrushes {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text, upload, download, muted;
    bool light = false;  // the palette the brushes hold
};

// Physical-pixel sizes at `dpi` for the widest plausible readout.
OverlaySizes MeasureOverlay(unsigned dpi, bool letters);

// The label that starts a readout line: an arrow, or the localized U:/D: label, plus a space.
std::wstring LinePrefix(bool upload, bool letters);

// How many leading characters of a line prefix take the label colour: all of it but trailing whitespace.
size_t PrefixColourLength(const std::wstring& prefix);

// Top of readout line `line` (0 = upload, 1 = download) in DIPs, for two lines of `lineHeight` DIPs centred
// in `height` DIPs. Both tops land on whole device pixels at `scale` device pixels per DIP.
float LineTopDip(float height, float lineHeight, float scale, int line);

// Width in DIPs of one readout line laid out in the overlay font (exposed for tests).
float LineWidthDip(const std::wstring& line);

// Draws the readout into a DC render target already bound to a 32bpp premultiplied DIB of `size`.
void RenderOverlay(ID2D1DCRenderTarget* rt, OverlayBrushes& brushes, SIZE size, const OverlayContent& c);

}  // namespace nsm
