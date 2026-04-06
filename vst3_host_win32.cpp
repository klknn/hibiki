#include <windows.h>

#include <atomic>
#include <iostream>
#include <thread>

#include "pluginterfaces/gui/iplugview.h"
#include "vst3_host.hpp"
#include "vst3_host_impl.hpp"

namespace hibiki {

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

  if (impl->editorRunning) return;
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

    if (view->attached((void*)hwnd, Steinberg::kPlatformTypeHWND) !=
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
      if (!impl->editorRunning) break;
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

std::vector<std::string> Vst3Plugin::getDefaultVst3Dirs() {
  std::vector<std::string> dirs;
  dirs.push_back("C:\\Program Files\\Common Files\\VST3");
  const char* userProfile = getenv("USERPROFILE");
  if (userProfile) {
    dirs.push_back(std::string(userProfile) + "\\.vst3");
  }
  return dirs;
}

bool Vst3Plugin::captureEditorFrame(std::vector<uint8_t>& rgba, int& w,
                                    int& h) {
  uint64_t win = impl->editorWindow.load();
  if (!win || !impl->editorRunning) return false;

  HWND hwnd = (HWND)win;
  RECT rc;
  if (!GetClientRect(hwnd, &rc)) return false;

  w = rc.right - rc.left;
  h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) return false;

  HDC hdcWin = GetDC(hwnd);
  HDC hdcMem = CreateCompatibleDC(hdcWin);

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;  // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!hbm) {
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWin);
    return false;
  }

  HGDIOBJ old = SelectObject(hdcMem, hbm);
  BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);

  // Convert BGRA → RGBA
  rgba.resize(w * h * 4);
  uint8_t* src = (uint8_t*)bits;
  for (int i = 0; i < w * h; ++i) {
    rgba[i * 4 + 0] = src[i * 4 + 2];  // R
    rgba[i * 4 + 1] = src[i * 4 + 1];  // G
    rgba[i * 4 + 2] = src[i * 4 + 0];  // B
    rgba[i * 4 + 3] = 0xFF;            // A
  }

  SelectObject(hdcMem, old);
  DeleteObject(hbm);
  DeleteDC(hdcMem);
  ReleaseDC(hwnd, hdcWin);
  return true;
}

void Vst3Plugin::sendEditorInput(int type, int x, int y, int button,
                                 int /*key_code*/, int /*delta*/) {
  uint64_t win = impl->editorWindow.load();
  if (!win || !impl->editorRunning) return;

  HWND hwnd = (HWND)win;
  LPARAM lParam = MAKELPARAM(x, y);

  switch (type) {
    case 0:  // MOUSE_MOVE
      PostMessage(hwnd, WM_MOUSEMOVE, 0, lParam);
      break;
    case 1:  // MOUSE_DOWN
      if (button == 1)
        PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
      else if (button == 3)
        PostMessage(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, lParam);
      break;
    case 2:  // MOUSE_UP
      if (button == 1)
        PostMessage(hwnd, WM_LBUTTONUP, 0, lParam);
      else if (button == 3)
        PostMessage(hwnd, WM_RBUTTONUP, 0, lParam);
      break;
    default:
      break;
  }
}

}  // namespace hibiki
