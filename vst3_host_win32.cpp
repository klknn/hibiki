#include <atomic>
#include <iostream>
#include <thread>
#include <windows.h>


#include "pluginterfaces/gui/iplugview.h"
#include "vst3_host.hpp"
#include "vst3_host_impl.hpp"


static LRESULT CALLBACK VstWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                      LPARAM lParam) {
  switch (uMsg) {
  case WM_CLOSE:
    PostQuitMessage(0);
    return 0;
  case WM_DESTROY:
    return 0;
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void Vst3Plugin::showEditor() {
  if (!impl->controller) {
    std::cerr << "No controller available for showing editor" << std::endl;
    return;
  }

  if (impl->editorRunning)
    return;
  stopEditor();

  impl->editorRunning = true;
  impl->editorThread = std::thread([this]() {
    Steinberg::IPtr<Steinberg::IPlugView> view = Steinberg::owned(
        impl->controller->createView(Steinberg::Vst::ViewType::kEditor));
    if (!view) {
      std::cerr << "Plugin does not provide an editor view" << std::endl;
      impl->editorRunning = false;
      return;
    }

    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASS wc = {0};
    wc.lpfnWndProc = VstWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "HibikiVstEditor";
    RegisterClass(&wc);

    Steinberg::ViewRect rect;
    view->getSize(&rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    HWND hwnd =
        CreateWindowEx(0, wc.lpszClassName, "Vst3 Plugin Editor",
                       WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                       width + 16, height + 39, NULL, NULL, hInstance, NULL);

    if (!hwnd) {
      std::cerr << "Failed to create window" << std::endl;
      impl->editorRunning = false;
      return;
    }

    impl->editorWindow = (uint64_t)hwnd;
    ShowWindow(hwnd, SW_SHOW);

    if (view->attached((void *)hwnd, Steinberg::kPlatformTypeHWND) !=
        Steinberg::kResultTrue) {
      std::cerr << "Failed to attach view to HWND" << std::endl;
      DestroyWindow(hwnd);
      impl->editorRunning = false;
      return;
    }

    MSG msg;
    while (impl->editorRunning) {
      while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          impl->editorRunning = false;
          break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      if (!impl->editorRunning)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    view->removed();
    DestroyWindow(hwnd);
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
