#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "absl/log/log.h"
#include "engine/vst3/vst3_host.hpp"
#include "engine/vst3/vst3_host_impl.hpp"
#include "pluginterfaces/gui/iplugview.h"

namespace hibiki {

void Vst3Plugin::showEditor() {
  if (!impl->controller) {
    LOG(INFO) << "No controller available for showing editor";
    return;
  }

  if (impl->editorRunning) return;

  // Clean up any old finished thread
  stopEditor();

  impl->editorRunning = true;
  impl->editorThread = std::thread([this]() {
    Steinberg::IPtr<Steinberg::IPlugView> view = Steinberg::owned(
        impl->controller->createView(Steinberg::Vst::ViewType::kEditor));
    if (!view) {
      LOG(INFO) << "Plugin does not provide an editor view";
      impl->editorRunning = false;
      return;
    }

    Display* display = XOpenDisplay(NULL);
    if (!display) {
      LOG(ERROR) << "Cannot open X display";
      impl->editorRunning = false;
      return;
    }

    Steinberg::ViewRect rect;
    if (view->getSize(&rect) != Steinberg::kResultTrue) {
      LOG(ERROR) << "Cannot get view size";
      XCloseDisplay(display);
      impl->editorRunning = false;
      return;
    }

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    LOG(INFO) << "Plugin View Size: " << width << "x" << height;

    int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(
        display, RootWindow(display, screen), 0, 0, width, height, 1,
        BlackPixel(display, screen), WhitePixel(display, screen));

    impl->editorWindow = (uint64_t)window;

    XStoreName(display, window, "Vst3 Plugin Editor");
    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | StructureNotifyMask |
                     SubstructureNotifyMask);

    // Intercept window close request
    Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDeleteMessage, 1);

    XMapWindow(display, window);
    XFlush(display);
    LOG(INFO) << "X11 Window created and mapped";

    if (view->attached((void*)window,
                       Steinberg::kPlatformTypeX11EmbedWindowID) !=
        Steinberg::kResultTrue) {
      LOG(ERROR) << "Failed to attach view to X11 window";
      XDestroyWindow(display, window);
      XCloseDisplay(display);
      impl->editorRunning = false;
      return;
    }
    LOG(INFO) << "Plugin View attached successfully";

    XEvent event;
    bool windowWasDestroyed = false;
    while (impl->editorRunning) {
      while (impl->editorRunning && XPending(display)) {
        XNextEvent(display, &event);
        if (event.type == DestroyNotify &&
            (event.xdestroywindow.window == window)) {
          LOG(INFO) << "X11 Window destroyed by WM";
          windowWasDestroyed = true;
          impl->editorRunning = false;
          break;
        }
        if (event.type == ClientMessage) {
          if ((Atom)event.xclient.data.l[0] == wmDeleteMessage) {
            LOG(INFO) << "X11 Close button clicked";
            impl->editorRunning = false;
            break;
          }
        }
      }
      if (!impl->editorRunning) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LOG(INFO) << "Cleaning up VST3 view...";
    view->removed();

    if (!windowWasDestroyed) {
      XDestroyWindow(display, window);
      XSync(display,
            False);  // Ensure destruction finishes before closing display
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

std::vector<std::string> Vst3Plugin::getDefaultVst3Dirs() {
  std::vector<std::string> dirs;
  dirs.push_back("/usr/lib/vst3");
  dirs.push_back("/usr/local/lib/vst3");
  const char* home = getenv("HOME");
  if (home) {
    dirs.push_back(std::string(home) + "/.vst3");
  }
  return dirs;
}

bool Vst3Plugin::captureEditorFrame(std::vector<uint8_t>& rgba, int& w,
                                    int& h) {
  uint64_t win = impl->editorWindow.load();
  if (!win || !impl->editorRunning) return false;

  Display* dpy = XOpenDisplay(NULL);
  if (!dpy) return false;

  XWindowAttributes attrs;
  if (!XGetWindowAttributes(dpy, (Window)win, &attrs)) {
    XCloseDisplay(dpy);
    return false;
  }

  w = attrs.width;
  h = attrs.height;
  XImage* img = XGetImage(dpy, (Window)win, 0, 0, w, h, AllPlanes, ZPixmap);
  if (!img) {
    XCloseDisplay(dpy);
    return false;
  }

  rgba.resize(w * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      unsigned long pixel = XGetPixel(img, x, y);
      int idx = (y * w + x) * 4;
      rgba[idx + 0] = (pixel >> 16) & 0xFF;  // R
      rgba[idx + 1] = (pixel >> 8) & 0xFF;   // G
      rgba[idx + 2] = pixel & 0xFF;          // B
      rgba[idx + 3] = 0xFF;                  // A
    }
  }

  XDestroyImage(img);
  XCloseDisplay(dpy);
  return true;
}

void Vst3Plugin::sendEditorInput(int type, int x, int y, int button,
                                 int /*key_code*/, int /*delta*/) {
  uint64_t win = impl->editorWindow.load();
  if (!win || !impl->editorRunning) return;

  Display* dpy = XOpenDisplay(NULL);
  if (!dpy) return;

  XEvent ev = {};
  Window w = (Window)win;

  switch (type) {
    case 0:  // MOUSE_MOVE
      ev.type = MotionNotify;
      ev.xmotion.window = w;
      ev.xmotion.x = x;
      ev.xmotion.y = y;
      break;
    case 1:  // MOUSE_DOWN
      ev.type = ButtonPress;
      ev.xbutton.window = w;
      ev.xbutton.x = x;
      ev.xbutton.y = y;
      ev.xbutton.button = button;
      break;
    case 2:  // MOUSE_UP
      ev.type = ButtonRelease;
      ev.xbutton.window = w;
      ev.xbutton.x = x;
      ev.xbutton.y = y;
      ev.xbutton.button = button;
      break;
    default:
      XCloseDisplay(dpy);
      return;
  }

  XSendEvent(dpy, w, True, NoEventMask, &ev);
  XFlush(dpy);
  XCloseDisplay(dpy);
}

}  // namespace hibiki
