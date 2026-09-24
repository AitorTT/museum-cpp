// Headless platform layer for the web build.
//
// There is no window object here: the page owns the canvas, and the frame loop
// is requestAnimationFrame. Everything SDL supplies on the desktop either comes
// from the browser (canvas size, pointer lock, raw mouse deltas) or from the
// JS bridge in web/shell.html.
#include "platform/window.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstring>

namespace museum::platform {
namespace {

// The engine registers itself here so the JS bridge can find it.
Window* g_window = nullptr;

}  // namespace

// Canvas resize is a DOM event; turn it into a pending resize for the engine.
EM_BOOL OnCanvasResize(int /*event_type*/, const EmscriptenUiEvent* /*event*/,
                       void* /*user_data*/);

Key KeyFromWebCode(const char* code) {
  if (code == nullptr) return Key::kUnknown;
  if (std::strcmp(code, "KeyW") == 0) return Key::kW;
  if (std::strcmp(code, "KeyA") == 0) return Key::kA;
  if (std::strcmp(code, "KeyS") == 0) return Key::kS;
  if (std::strcmp(code, "KeyD") == 0) return Key::kD;
  if (std::strcmp(code, "KeyQ") == 0) return Key::kQ;
  if (std::strcmp(code, "KeyE") == 0) return Key::kE;
  if (std::strcmp(code, "Space") == 0) return Key::kSpace;
  if (std::strcmp(code, "ShiftLeft") == 0) return Key::kLeftShift;
  if (std::strcmp(code, "Escape") == 0) return Key::kEscape;
  if (std::strcmp(code, "F1") == 0) return Key::kF1;
  if (std::strcmp(code, "ArrowUp") == 0) return Key::kUp;
  if (std::strcmp(code, "ArrowDown") == 0) return Key::kDown;
  if (std::strcmp(code, "ArrowLeft") == 0) return Key::kLeft;
  if (std::strcmp(code, "ArrowRight") == 0) return Key::kRight;
  return Key::kUnknown;
}

Window::Window() = default;
Window::~Window() = default;

bool Window::Create(const char* /*title*/, std::uint32_t width, std::uint32_t height) {
  g_window = this;

  int canvas_width = 0;
  int canvas_height = 0;
  if (emscripten_get_canvas_element_size("#canvas", &canvas_width, &canvas_height) ==
          EMSCRIPTEN_RESULT_SUCCESS &&
      canvas_width > 0 && canvas_height > 0) {
    width_ = static_cast<std::uint32_t>(canvas_width);
    height_ = static_cast<std::uint32_t>(canvas_height);
  } else {
    width_ = width;
    height_ = height;
  }

  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, 1,
                                 OnCanvasResize);
  return true;
}

void Window::PollEvents() {
  input_.ResetPerFrame();
  // Keyboard and pointer events arrive through the JS bridge between frames,
  // so there is nothing to drain. This is where touch UI and gamepad polling
  // hook in for the mobile build.
  input_.quit_requested = false;
}

void Window::EndFrame() { input_.ResetPerFrame(); }

void Window::RequestPointerLock() {
  EM_ASM({ document.getElementById('canvas').requestPointerLock(); });
  input_.pointer_locked = true;
}

void Window::ReleasePointerLock() {
  EM_ASM({ document.exitPointerLock(); });
  input_.pointer_locked = false;
}

bool Window::ConsumeResize(std::uint32_t* out_width, std::uint32_t* out_height) {
  if (!resize_pending_) {
    return false;
  }
  resize_pending_ = false;
  if (out_width != nullptr) *out_width = pending_width_;
  if (out_height != nullptr) *out_height = pending_height_;
  return true;
}

void Window::ApplyLookDelta(float dx, float dy) {
  input_.look_yaw += dx;
  input_.look_pitch += dy;
}

void Window::SetPointerLocked(bool locked) {
  if (!locked && input_.pointer_locked) {
    input_.pointer_lock_lost = true;
  }
  input_.pointer_locked = locked;
}

void Window::SetKey(Key key, bool down) {
  const auto index = static_cast<std::uint32_t>(key);
  if (index < kKeyCount) {
    input_.keys[index] = down;
  }
  if (key == Key::kEscape && down && input_.pointer_locked) {
    ReleasePointerLock();
  }
  UpdateMoveAxes();
}

void Window::UpdateMoveAxes() {
  const bool w = input_.KeyDown(Key::kW) || input_.KeyDown(Key::kUp);
  const bool s = input_.KeyDown(Key::kS) || input_.KeyDown(Key::kDown);
  const bool d = input_.KeyDown(Key::kD) || input_.KeyDown(Key::kRight);
  const bool a = input_.KeyDown(Key::kA) || input_.KeyDown(Key::kLeft);
  input_.move_forward = (w ? 1.0f : 0.0f) - (s ? 1.0f : 0.0f);
  input_.move_strafe = (d ? 1.0f : 0.0f) - (a ? 1.0f : 0.0f);
}

// Canvas resize is a DOM event; turn it into a pending resize for the engine.
EM_BOOL OnCanvasResize(int /*event_type*/, const EmscriptenUiEvent* /*event*/,
                       void* /*user_data*/) {
  if (g_window == nullptr) {
    return EM_FALSE;
  }
  int width = 0;
  int height = 0;
  emscripten_get_canvas_element_size("#canvas", &width, &height);
  if (width > 0 && height > 0) {
    g_window->SetPendingResize(static_cast<std::uint32_t>(width),
                               static_cast<std::uint32_t>(height));
  }
  return EM_TRUE;
}

}  // namespace museum::platform

// ---------------------------------------------------------------------------
// JS bridge. extern "C" keeps the names stable for web/shell.html.
// ---------------------------------------------------------------------------
extern "C" {

EMSCRIPTEN_KEEPALIVE void museumApplyLook(float dx, float dy) {
  if (museum::platform::g_window != nullptr) {
    museum::platform::g_window->ApplyLookDelta(dx, dy);
  }
}

EMSCRIPTEN_KEEPALIVE void museumSetPointerLocked(int locked) {
  if (museum::platform::g_window != nullptr) {
    museum::platform::g_window->SetPointerLocked(locked != 0);
  }
}

EMSCRIPTEN_KEEPALIVE void museumKey(const char* code, int down) {
  if (museum::platform::g_window != nullptr) {
    museum::platform::g_window->SetKey(museum::platform::KeyFromWebCode(code),
                                       down != 0);
  }
}

// The page passes DOM key codes as a number, keyed by a small table, so the JS
// side never has to marshal a string into the WASM heap.
EMSCRIPTEN_KEEPALIVE void museumKeyCode(int code_index, int down) {
  if (museum::platform::g_window == nullptr) {
    return;
  }
  static constexpr museum::platform::Key kCodeTable[] = {
      museum::platform::Key::kUnknown,
      museum::platform::Key::kW,
      museum::platform::Key::kA,
      museum::platform::Key::kS,
      museum::platform::Key::kD,
      museum::platform::Key::kQ,
      museum::platform::Key::kE,
      museum::platform::Key::kSpace,
      museum::platform::Key::kLeftShift,
      museum::platform::Key::kEscape,
      museum::platform::Key::kF1,
      museum::platform::Key::kUp,
      museum::platform::Key::kDown,
      museum::platform::Key::kLeft,
      museum::platform::Key::kRight,
  };
  constexpr int kTableSize =
      static_cast<int>(sizeof(kCodeTable) / sizeof(kCodeTable[0]));
  if (code_index > 0 && code_index < kTableSize) {
    museum::platform::g_window->SetKey(kCodeTable[code_index], down != 0);
  }
}

// Resize arrives from the page's resize handler; forward it to the window.
EMSCRIPTEN_KEEPALIVE void museumNotifyResize(int width, int height) {
  if (museum::platform::g_window != nullptr && width > 0 && height > 0) {
    museum::platform::g_window->SetPendingResize(
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
  }
}

}  // extern "C"

#endif  // __EMSCRIPTEN__
