// SDL3 implementation of the platform layer (desktop builds).
#ifdef MUSEUM_USE_SDL

#include "platform/window.h"

#include <SDL3/SDL.h>

#include <cstdio>

namespace museum::platform {
namespace {

Key TranslateKey(SDL_Keycode keycode) {
  switch (keycode) {
    case SDLK_W: return Key::kW;
    case SDLK_A: return Key::kA;
    case SDLK_S: return Key::kS;
    case SDLK_D: return Key::kD;
    case SDLK_Q: return Key::kQ;
    case SDLK_E: return Key::kE;
    case SDLK_SPACE: return Key::kSpace;
    case SDLK_LSHIFT: return Key::kLeftShift;
    case SDLK_ESCAPE: return Key::kEscape;
    case SDLK_F1: return Key::kF1;
    case SDLK_UP: return Key::kUp;
    case SDLK_DOWN: return Key::kDown;
    case SDLK_LEFT: return Key::kLeft;
    case SDLK_RIGHT: return Key::kRight;
    default: return Key::kUnknown;
  }
}

}  // namespace

Window::Window() = default;

Window::~Window() {
  if (impl_ != nullptr) {
    SDL_DestroyWindow(static_cast<SDL_Window*>(impl_));
    impl_ = nullptr;
  }
  SDL_Quit();
}

bool Window::Create(const char* title, std::uint32_t width, std::uint32_t height) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::printf("SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }

  SDL_Window* window = SDL_CreateWindow(title, static_cast<int>(width),
                                        static_cast<int>(height),
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window == nullptr) {
    std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
    return false;
  }

  impl_ = window;
  width_ = width;
  height_ = height;

  // What a native WebGPU surface is built from. Kept as opaque pointers so
  // this file stays the only place that knows about HWND/HINSTANCE.
  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  native_handle_ =
      SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  native_display_ =
      SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
  return true;
}

void Window::PollEvents() {
  input_.ResetPerFrame();

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_EVENT_QUIT:
        input_.quit_requested = true;
        break;

      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        int pixel_width = 0;
        int pixel_height = 0;
        SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(impl_), &pixel_width,
                                  &pixel_height);
        if (pixel_width > 0 && pixel_height > 0) {
          SetPendingResize(static_cast<std::uint32_t>(pixel_width),
                           static_cast<std::uint32_t>(pixel_height));
        }
        break;
      }

      case SDL_EVENT_KEY_DOWN: {
        const Key key = TranslateKey(event.key.key);
        if (key != Key::kUnknown) {
          input_.keys[static_cast<std::uint32_t>(key)] = true;
        }
        if (key == Key::kEscape && input_.pointer_locked) {
          // Esc drops pointer lock; flag it so the UI can show "click to resume".
          ReleasePointerLock();
          input_.pointer_lock_lost = true;
        }
        break;
      }

      case SDL_EVENT_KEY_UP: {
        const Key key = TranslateKey(event.key.key);
        if (key != Key::kUnknown) {
          input_.keys[static_cast<std::uint32_t>(key)] = false;
        }
        break;
      }

      case SDL_EVENT_MOUSE_MOTION:
        if (input_.pointer_locked) {
          input_.look_yaw += event.motion.xrel;
          input_.look_pitch += event.motion.yrel;
        }
        break;

      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!input_.pointer_locked) {
          RequestPointerLock();
        }
        break;

      default:
        break;
    }
  }

  // Keyboard is merged with any attached gamepad so Player has one source of
  // truth. Sticks are sampled every frame rather than through events.
  UpdateMoveAxes();
  for (int pad = 0; pad < 4; ++pad) {
    SDL_Gamepad* gamepad = SDL_GetGamepadFromID(
        static_cast<SDL_JoystickID>(pad));
    (void)gamepad;  // wired in the mobile/controller batch
  }

  input_.pointer_locked =
      SDL_GetWindowRelativeMouseMode(static_cast<SDL_Window*>(impl_));
}

void Window::UpdateMoveAxes() {
  const bool* keys = SDL_GetKeyboardState(nullptr);

  const bool w = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
  const bool s = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
  const bool d = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
  const bool a = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];

  input_.move_forward = (w ? 1.0f : 0.0f) - (s ? 1.0f : 0.0f);
  input_.move_strafe = (d ? 1.0f : 0.0f) - (a ? 1.0f : 0.0f);

  // Mirror the polled state into the key table so KeyDown() stays accurate
  // even when SDL's event queue and the keyboard-state array disagree.
  input_.keys[static_cast<std::uint32_t>(Key::kW)] = w;
  input_.keys[static_cast<std::uint32_t>(Key::kS)] = s;
  input_.keys[static_cast<std::uint32_t>(Key::kD)] = d;
  input_.keys[static_cast<std::uint32_t>(Key::kA)] = a;
}

void Window::EndFrame() { input_.ResetPerFrame(); }

void Window::RequestPointerLock() {
  if (impl_ == nullptr) {
    return;
  }
  if (!SDL_SetWindowRelativeMouseMode(static_cast<SDL_Window*>(impl_), true)) {
    std::printf("Pointer lock failed: %s\n", SDL_GetError());
    return;
  }
  input_.pointer_locked = true;
}

void Window::ReleasePointerLock() {
  if (impl_ == nullptr) {
    return;
  }
  SDL_SetWindowRelativeMouseMode(static_cast<SDL_Window*>(impl_), false);
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

}  // namespace museum::platform

#endif  // MUSEUM_USE_SDL
