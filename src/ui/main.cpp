#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <cmath>

#include "core/speed_history.h"
#include "platform/strings.h"
#include "resource.h"
#include "ui/app.h"
#include "ui/overlay_manager.h"

namespace nsm {
namespace {

constexpr wchar_t kDemoClass[] = L"NetworkSpeedMonitor.DemoWindow";

// --demo [--all-taskbars] [--demo-seconds N]: the overlay manager fed with sine-wave speeds.
// Left-click toggles the graph, right-click toggles arrows/letters. The demo ends cleanly (and
// so restores the Windows 10 task list) after N seconds, or when its window gets WM_CLOSE,
// e.g. from `taskkill /im NetworkSpeedMonitor.exe` without /f.
class Demo {
public:
    Demo() = default;
    ~Demo() {
        if (hwnd_) DestroyWindow(hwnd_);  // Start failed after creating it
    }
    Demo(const Demo&) = delete;
    Demo& operator=(const Demo&) = delete;

    bool Start(HINSTANCE inst, bool allTaskbars, unsigned seconds) {
        seconds_ = seconds;
        options_.allTaskbars = allTaskbars;
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &Demo::WndProc;
        wc.hInstance = inst;
        wc.lpszClassName = kDemoClass;
        if (!RegisterClassExW(&wc)) return false;
        // Hidden top-level window, not HWND_MESSAGE: it must get the TaskbarCreated broadcast.
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kDemoClass, LoadStr(IDS_APP_NAME).c_str(), WS_POPUP, 0, 0, 0, 0,
                                nullptr, nullptr, inst, this);
        if (!hwnd_) return false;

        manager_.onLeftClick = [this] {
            options_.showGraph = !options_.showGraph;
            manager_.SetOptions(options_);
        };
        manager_.onRightClick = [this](POINT) {
            options_.letters = !options_.letters;
            manager_.SetOptions(options_);
        };
        manager_.SetOptions(options_);
        if (!manager_.Init(inst, hwnd_)) return false;
        manager_.SetTooltip(LoadStr(IDS_APP_NAME));
        Tick();
        return SetTimer(hwnd_, 1, 1000, nullptr) != 0;
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto* self = reinterpret_cast<Demo*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        switch (msg) {
            case WM_TIMER:
                self->Tick();
                if (self->seconds_ && self->ticks_ >= self->seconds_) PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            case WM_DESTROY:
                KillTimer(hwnd, 1);
                self->manager_.Shutdown();  // restores the Win10 task list
                self->hwnd_ = nullptr;
                PostQuitMessage(0);
                return 0;
            default:
                if (self->manager_.HandleMessage(msg, wp, lp)) return 0;
                if (msg == WM_ENDSESSION && wp) DestroyWindow(hwnd);
                return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

    void Tick() {
        const double t = static_cast<double>(++ticks_);
        const Speed speed{2.5e6 * (1.0 + std::sin(t * 0.21)) + 2e3, 3.0e5 * (1.0 + std::sin(t * 0.37 + 1.0)) + 5e2};
        history_.Push(speed);
        manager_.Update(speed, history_);
    }

    HWND hwnd_ = nullptr;
    OverlayManager manager_;
    OverlayOptions options_;
    SpeedHistory history_;
    unsigned ticks_ = 0;
    unsigned seconds_ = 0;  // 0: run until closed
};

// Index of `wanted` in argv, or 0 if absent.
int FindArg(int argc, LPWSTR* argv, const wchar_t* wanted) {
    for (int i = 1; argv && i < argc; ++i) {
        if (wcscmp(argv[i], wanted) == 0) return i;
    }
    return 0;
}

int RunDemo(HINSTANCE inst) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool allTaskbars = FindArg(argc, argv, L"--all-taskbars") != 0;
    const int secondsAt = FindArg(argc, argv, L"--demo-seconds");
    const unsigned seconds = secondsAt && secondsAt + 1 < argc ? static_cast<unsigned>(_wtoi(argv[secondsAt + 1])) : 0;
    LocalFree(argv);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;  // UI Automation needs an STA
    int code = 1;
    {
        Demo demo;
        if (demo.Start(inst, allTaskbars, seconds)) {
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

bool HasArg(const wchar_t* wanted) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool found = FindArg(argc, argv, wanted) != 0;
    LocalFree(argv);
    return found;
}

}  // namespace
}  // namespace nsm

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    if (nsm::HasArg(L"--demo")) return nsm::RunDemo(inst);
    nsm::App app;
    return app.Run(inst, GetCommandLineW());
}
