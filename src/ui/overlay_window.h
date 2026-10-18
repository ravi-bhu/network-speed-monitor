#pragma once
#include <windows.h>
#include <d2d1.h>

#include <functional>

#include "core/overlay_layout.h"
#include "ui/overlay_renderer.h"

namespace nsm {

// A layered, topmost, non-activating popup that shows the readout at a given placement.
// WM_CLOSE is forwarded to the owner window.
class OverlayWindow {
public:
    OverlayWindow() = default;
    ~OverlayWindow();
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool Create(HINSTANCE inst, HWND owner);
    // Shows at p.rect (or hides when !p.visible). The graph is drawn iff p.showGraph,
    // since the placement width decides whether it fits. A hidden window is shown as topmost;
    // an already visible one keeps its place in the z-order.
    void Show(const OverlayPlacement& p, const OverlayContent& c);
    void Hide();
    void RaiseTopmost();  // to the top of the topmost band, if visible; never activates
    bool Visible() const;
    HWND Hwnd() const { return hwnd_; }

    std::function<void()> onLeftClick;
    std::function<void(POINT screenPt)> onRightClick;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    bool EnsureSurface(SIZE size);
    void ReleaseSurface();

    HWND hwnd_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    SIZE dibSize_{};
    ID2D1DCRenderTarget* rt_ = nullptr;
};

}  // namespace nsm
