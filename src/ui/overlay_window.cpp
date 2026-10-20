#include "ui/overlay_window.h"

#include <windowsx.h>

namespace nsm {
namespace {

constexpr wchar_t kClassName[] = L"NetworkSpeedMonitorOverlay";

ID2D1Factory* D2DFactory() {
    static ID2D1Factory* factory = [] {
        ID2D1Factory* f = nullptr;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &f);
        return f;
    }();
    return factory;
}

}  // namespace

OverlayWindow::~OverlayWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
    ReleaseSurface();
    if (rt_) rt_->Release();
    if (memDc_) DeleteDC(memDc_);
}

bool OverlayWindow::Create(HINSTANCE inst, HWND owner) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &OverlayWindow::WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClassName, L"",
                            WS_POPUP, 0, 0, 0, 0, owner, nullptr, inst, this);
    if (!hwnd_) return false;

    memDc_ = CreateCompatibleDC(nullptr);
    ID2D1Factory* factory = D2DFactory();
    if (!memDc_ || !factory) return false;
    // Software rendering: no GPU device to lose, and a much smaller working set for a tiny surface.
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    return SUCCEEDED(factory->CreateDCRenderTarget(&props, &rt_));
}

void OverlayWindow::Show(const OverlayPlacement& p, const OverlayContent& c) {
    if (!hwnd_ || !rt_) return;
    if (!p.visible) {
        Hide();
        return;
    }
    SIZE size{p.rect.Width(), p.rect.Height()};
    if (!EnsureSurface(size)) return;

    const RECT bounds{0, 0, size.cx, size.cy};
    if (FAILED(rt_->BindDC(memDc_, &bounds))) return;
    OverlayContent content = c;
    content.showGraph = p.showGraph;
    // A failed draw leaves the DIB stale or blank: keep the last good frame on screen instead.
    if (FAILED(RenderOverlay(rt_, size, content))) return;

    POINT dst{p.rect.left, p.rect.top};
    POINT src{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(hwnd_, nullptr, &dst, &size, memDc_, &src, 0, &blend, ULW_ALPHA)) return;
    // Topmost is asserted only when the window appears; afterwards the owner decides when to
    // re-assert it (RaiseTopmost), so a fullscreen app or a shell flyout is never covered.
    if (!IsWindowVisible(hwnd_))
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void OverlayWindow::Hide() {
    if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();  // a press on a hidden overlay is no click
    if (hwnd_ && IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_HIDE);
}

void OverlayWindow::RaiseTopmost() {
    if (hwnd_ && IsWindowVisible(hwnd_))
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool OverlayWindow::Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

bool OverlayWindow::EnsureSurface(SIZE size) {
    if (size.cx <= 0 || size.cy <= 0) return false;
    if (dib_ && size.cx == dibSize_.cx && size.cy == dibSize_.cy) return true;
    ReleaseSurface();

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = size.cx;
    bi.bmiHeader.biHeight = -size.cy;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    dib_ = CreateDIBSection(memDc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib_) return false;
    oldBitmap_ = SelectObject(memDc_, dib_);
    dibSize_ = size;
    return true;
}

void OverlayWindow::ReleaseSurface() {
    if (!dib_) return;
    SelectObject(memDc_, oldBitmap_);
    DeleteObject(dib_);
    dib_ = nullptr;
    oldBitmap_ = nullptr;
    dibSize_ = {};
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* created = static_cast<OverlayWindow*>(cs->lpCreateParams);
        created->hwnd_ = hwnd;  // CreateWindowExW has not returned yet
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
    }
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self->HandleMessage(msg, wp, lp);
}

LRESULT OverlayWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            clicks_.Down(msg == WM_LBUTTONDOWN ? MouseButton::Left : MouseButton::Right);
            if (GetCapture() != hwnd_) SetCapture(hwnd_);  // the button-up arrives here even off the overlay
            return 0;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT client{};
            GetClientRect(hwnd_, &client);
            const MouseButton button = msg == WM_LBUTTONUP ? MouseButton::Left : MouseButton::Right;
            const bool click = clicks_.Up(button, PtInRect(&client, pt) != FALSE);
            // Release before the callbacks: a right click opens a menu, which needs the mouse.
            if (!clicks_.Pending() && GetCapture() == hwnd_) ReleaseCapture();
            if (!click) return 0;
            if (msg == WM_LBUTTONUP) {
                if (onLeftClick) onLeftClick();
            } else {
                ClientToScreen(hwnd_, &pt);
                if (onRightClick) onRightClick(pt);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lp) != hwnd_) clicks_.Cancel();
            return 0;
        case WM_DPICHANGED:
            return 0;  // size and position come from the taskbar probe, not from the suggested rect
        case WM_CLOSE:
            // e.g. `taskkill` without /f: let the owner (the app window) close the app instead of
            // destroying one overlay under its manager.
            if (HWND owner = GetWindow(hwnd_, GW_OWNER)) PostMessageW(owner, WM_CLOSE, 0, 0);
            return 0;
        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

}  // namespace nsm
