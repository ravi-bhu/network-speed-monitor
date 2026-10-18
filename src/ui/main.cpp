#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <cmath>
#include <optional>

#include "core/overlay_layout.h"
#include "core/speed_history.h"
#include "platform/strings.h"
#include "platform/theme.h"
#include "resource.h"
#include "ui/overlay_renderer.h"
#include "ui/overlay_window.h"
#include "ui/taskbar_probe.h"

namespace nsm {
namespace {

// --demo: one overlay on the primary taskbar, fed with sine-wave speeds.
// Left-click toggles the graph, right-click toggles arrows/letters.
class Demo {
public:
    Demo() { instance_ = this; }
    ~Demo() { instance_ = nullptr; }
    Demo(const Demo&) = delete;
    Demo& operator=(const Demo&) = delete;

    bool Start(HINSTANCE inst) {
        if (!overlay_.Create(inst, nullptr)) return false;
        SetWindowTextW(overlay_.Hwnd(), LoadStr(IDS_APP_NAME).c_str());
        overlay_.onLeftClick = [this] {
            graph_ = !graph_;
            Update();
        };
        overlay_.onRightClick = [this](POINT) {
            letters_ = !letters_;
            Update();
        };
        Tick();
        return SetTimer(nullptr, 0, 1000, &Demo::OnTimer) != 0;
    }

private:
    static void CALLBACK OnTimer(HWND, UINT, UINT_PTR, DWORD) { instance_->Tick(); }

    void Tick() {
        const double t = static_cast<double>(++ticks_);
        speed_ = Speed{2.5e6 * (1.0 + std::sin(t * 0.21)) + 2e3, 3.0e5 * (1.0 + std::sin(t * 0.37 + 1.0)) + 5e2};
        history_.Push(*speed_);
        if (ticks_ % 2 == 1) Probe();  // every 2 s, starting with the first tick
        Update();
    }

    void Probe() {
        geom_.reset();
        for (const TaskbarInfo& bar : ProbeTaskbars()) {
            if (bar.primary) geom_ = bar.geom;
        }
    }

    void Update() {
        if (!geom_) {
            overlay_.Hide();
            return;
        }
        if (!sizes_ || sizesDpi_ != geom_->dpi || sizesLetters_ != letters_) {
            sizes_ = MeasureOverlay(geom_->dpi, letters_);
            sizesDpi_ = geom_->dpi;
            sizesLetters_ = letters_;
        }
        const OverlayPlacement placement = PlaceOverlay(*geom_, *sizes_, graph_);
        const OverlayContent content{
            speed_, UnitMode::Bytes, letters_, placement.showGraph, &history_, IsTaskbarLight(), geom_->dpi};
        overlay_.Show(placement, content);
    }

    static inline Demo* instance_ = nullptr;  // for the TIMERPROC

    OverlayWindow overlay_;
    SpeedHistory history_;
    std::optional<Speed> speed_;
    std::optional<TaskbarGeometry> geom_;
    std::optional<OverlaySizes> sizes_;
    unsigned sizesDpi_ = 0;
    bool sizesLetters_ = false;
    bool graph_ = true;
    bool letters_ = false;
    unsigned ticks_ = 0;
};

bool HasArg(const wchar_t* wanted) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool found = false;
    for (int i = 1; argv && i < argc; ++i) found = found || wcscmp(argv[i], wanted) == 0;
    LocalFree(argv);
    return found;
}

int RunDemo(HINSTANCE inst) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;  // UI Automation needs an STA
    int code = 1;
    {
        Demo demo;
        if (demo.Start(inst)) {
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            code = static_cast<int>(msg.wParam);
        }
    }
    CoUninitialize();
    return code;
}

}  // namespace
}  // namespace nsm

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    if (nsm::HasArg(L"--demo")) return nsm::RunDemo(inst);
    return 0;  // The real app (sampling, menu, report) is wired up in a later task.
}
