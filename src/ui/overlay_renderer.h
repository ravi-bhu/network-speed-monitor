#pragma once
#include <windows.h>
#include <d2d1.h>

#include <optional>

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

// Physical-pixel sizes at `dpi` for the widest plausible readout.
OverlaySizes MeasureOverlay(unsigned dpi, bool letters);

// The label that starts a readout line: an arrow, or the localized U:/D: label, plus a space.
std::wstring LinePrefix(bool upload, bool letters);

// Width in DIPs of one readout line laid out in the overlay font (exposed for tests).
float LineWidthDip(const std::wstring& line);

// Draws the readout into a DC render target already bound to a 32bpp premultiplied DIB of `size`.
// Returns the first failure (a brush, or EndDraw); on failure the DIB must not be shown.
HRESULT RenderOverlay(ID2D1DCRenderTarget* rt, SIZE size, const OverlayContent& c);

}  // namespace nsm
