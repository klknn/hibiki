#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <memory>

#include "vst3_editor.hpp"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"

class Vst3EditorX11 : public IVst3Editor {
public:
    bool open(Steinberg::Vst::IEditController* controller, std::atomic<bool>& running, std::atomic<uint64_t>& window_id) override {
        if (!controller) return false;

        Steinberg::IPtr<Steinberg::IPlugView> view = Steinberg::owned(controller->createView(Steinberg::Vst::ViewType::kEditor));
        if (!view) {
            std::cerr << "Plugin does not provide an editor view" << std::endl;
            running = false;
            return false;
        }

        Display* display = XOpenDisplay(NULL);
        if (!display) {
            std::cerr << "Cannot open X display" << std::endl;
            running = false;
            return false;
        }

        Steinberg::ViewRect rect;
        if (view->getSize(&rect) != Steinberg::kResultTrue) {
            std::cerr << "Cannot get view size" << std::endl;
            XCloseDisplay(display);
            running = false;
            return false;
        }

        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        std::cout << "Plugin View Size: " << width << "x" << height << std::endl;

        int screen = DefaultScreen(display);
        Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, width, height, 1,
                                          BlackPixel(display, screen), WhitePixel(display, screen));

        window_id = (uint64_t)window;

        XStoreName(display, window, "Vst3 Plugin Editor");
        XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask | SubstructureNotifyMask);

        // Intercept window close request
        Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display, window, &wmDeleteMessage, 1);

        XMapWindow(display, window);
        XFlush(display);
        std::cout << "X11 Window created and mapped" << std::endl;

        if (view->attached((void*)window, Steinberg::kPlatformTypeX11EmbedWindowID) != Steinberg::kResultTrue) {
            std::cerr << "Failed to attach view to X11 window" << std::endl;
            XDestroyWindow(display, window);
            XCloseDisplay(display);
            running = false;
            return false;
        }
        std::cout << "Plugin View attached successfully" << std::endl;

        XEvent event;
        bool windowWasDestroyed = false;
        while (running) {
            while (running && XPending(display)) {
                XNextEvent(display, &event);
                if (event.type == DestroyNotify && (event.xdestroywindow.window == window)) {
                    std::cout << "X11 Window destroyed by WM" << std::endl;
                    windowWasDestroyed = true;
                    running = false;
                    break;
                }
                if (event.type == ClientMessage) {
                    if ((Atom)event.xclient.data.l[0] == wmDeleteMessage) {
                        std::cout << "X11 Close button clicked" << std::endl;
                        running = false;
                        break;
                    }
                }
            }
            if (!running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::cout << "Cleaning up VST3 view..." << std::endl;
        view->removed();
        
        if (!windowWasDestroyed) {
            XDestroyWindow(display, window);
            XSync(display, False); // Ensure destruction finishes before closing display
        }
        XCloseDisplay(display);
        window_id = 0;
        running = false;
        return true;
    }
};

std::unique_ptr<IVst3Editor> createVst3Editor() {
    return std::make_unique<Vst3EditorX11>();
}
