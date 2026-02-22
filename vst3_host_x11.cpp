#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

#include "vst3_host.hpp"
#include "vst3_host_impl.hpp"
#include "pluginterfaces/gui/iplugview.h"

void Vst3Plugin::showEditor() {
    if (!impl->controller) {
        std::cerr << "No controller available for showing editor" << std::endl;
        return;
    }

    if (impl->editorRunning) return;

    // Clean up any old finished thread
    stopEditor();

    impl->editorRunning = true;
    impl->editorThread = std::thread([this]() {
        Steinberg::IPtr<Steinberg::IPlugView> view = Steinberg::owned(impl->controller->createView(Steinberg::Vst::ViewType::kEditor));
        if (!view) {
            std::cerr << "Plugin does not provide an editor view" << std::endl;
            impl->editorRunning = false;
            return;
        }


        Display* display = XOpenDisplay(NULL);
        if (!display) {
            std::cerr << "Cannot open X display" << std::endl;
            impl->editorRunning = false;
            return;
        }

        Steinberg::ViewRect rect;
        if (view->getSize(&rect) != Steinberg::kResultTrue) {
            std::cerr << "Cannot get view size" << std::endl;
            XCloseDisplay(display);
            impl->editorRunning = false;
            return;
        }

        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        std::cerr << "Plugin View Size: " << width << "x" << height << std::endl;

        int screen = DefaultScreen(display);
        Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, width, height, 1,
                                          BlackPixel(display, screen), WhitePixel(display, screen));

        impl->editorWindow = (uint64_t)window;

        XStoreName(display, window, "Vst3 Plugin Editor");
        XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask | SubstructureNotifyMask);

        // Intercept window close request
        Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display, window, &wmDeleteMessage, 1);

        XMapWindow(display, window);
        XFlush(display);
        std::cerr << "X11 Window created and mapped" << std::endl;


        if (view->attached((void*)window, Steinberg::kPlatformTypeX11EmbedWindowID) != Steinberg::kResultTrue) {
            std::cerr << "Failed to attach view to X11 window" << std::endl;
            XDestroyWindow(display, window);
            XCloseDisplay(display);
            impl->editorRunning = false;
            return;
        }
        std::cerr << "Plugin View attached successfully" << std::endl;

        XEvent event;
        bool windowWasDestroyed = false;
        while (impl->editorRunning) {
            while (impl->editorRunning && XPending(display)) {
                XNextEvent(display, &event);
                if (event.type == DestroyNotify && (event.xdestroywindow.window == window)) {
                    std::cerr << "X11 Window destroyed by WM" << std::endl;
                    windowWasDestroyed = true;
                    impl->editorRunning = false;
                    break;
                }
                if (event.type == ClientMessage) {
                    if ((Atom)event.xclient.data.l[0] == wmDeleteMessage) {
                        std::cerr << "X11 Close button clicked" << std::endl;
                        impl->editorRunning = false;
                        break;
                    }
                }
            }
            if (!impl->editorRunning) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::cerr << "Cleaning up VST3 view..." << std::endl;
        view->removed();
        
        if (!windowWasDestroyed) {
            XDestroyWindow(display, window);
            XSync(display, False); // Ensure destruction finishes before closing display
        }
        XCloseDisplay(display);
        impl->editorWindow = 0;
        impl->editorRunning = false;
    });
}


void Vst3Plugin::stopEditor() {
    if (impl->editorThread.joinable()) {
        impl->editorRunning = false;
        impl->editorThread.join();
    } else {
        impl->editorRunning = false;
    }
}
