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
};

// Physical-pixel sizes at `dpi` for the widest plausible readout.
OverlaySizes MeasureOverlay(unsigned dpi, bool letters);

// Draws the readout into a DC render target already bound to a 32bpp premultiplied DIB of `size`.
void RenderOverlay(ID2D1DCRenderTarget* rt, SIZE size, const OverlayContent& c);

}  // namespace nsm
