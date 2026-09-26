// The platform layer: a window, an event loop, and input state.
//
// Deliberately free of Win32 and of GPU types, the same discipline used in
// carver_core. Everything above this header talks to Window and never learns
// whether SDL3 or the browser is underneath.
#pragma once

#include <cstdint>

#include "core/math.h"

namespace museum::platform {

enum class Key {
  kUnknown = 0,
  kW,
  kA,
  kS,
  kD,
  kQ,
  kE,
  kSpace,
  kLeftShift,
  kEscape,
  kF1,
  kUp,
  kDown,
  kLeft,
  kRight,
  kCount,
};

inline constexpr std::uint32_t kKeyCount = static_cast<std::uint32_t>(Key::kCount);

struct InputState {
  // -1..1 per axis, already normalized for the player controller. Written by
  // the keyboard (via UpdateMoveAxes) or, on touch devices, by the on-screen
  // stick straight through SetStick.
  float move_forward = 0.0f;
  float move_strafe = 0.0f;

  // True while an analog source owns the movement axes. Without it, a key
  // release would call UpdateMoveAxes, see every key up, and zero the stick the
  // user is still holding.
  bool stick_active = false;

  // Right-stick deflection for looking, -1..1, when the touch look stick is
  // held. Unlike the mouse, which arrives as accumulated deltas, this is a
  // *rate*: the engine turns it into a per-frame delta using dt, so a held
  // stick rotates steadily and a released one stops.
  float look_stick_x = 0.0f;
  float look_stick_y = 0.0f;
  bool look_stick_active = false;

  // Accumulated since the last frame, in pixels. Drained by Player::Update.
  float look_yaw = 0.0f;
  float look_pitch = 0.0f;

  bool pointer_locked = false;
  bool quit_requested = false;
  bool pointer_lock_lost = false;  // the user hit Esc while playing

  bool KeyDown(Key key) const {
    const auto index = static_cast<std::uint32_t>(key);
    return index < kKeyCount && keys[index];
  }

  // Clears everything that is inherently per-frame.
  void ResetPerFrame() {
    look_yaw = 0.0f;
    look_pitch = 0.0f;
    pointer_lock_lost = false;
  }

  bool keys[kKeyCount] = {};
};

// A window plus the events that arrive on it. Implemented twice:
// window_sdl.cpp (desktop) and window_headless.cpp (Emscripten, where the page
// owns the canvas and the frame loop is requestAnimationFrame).
class Window {
 public:
  static constexpr std::uint32_t kDefaultWidth = 1280;
  static constexpr std::uint32_t kDefaultHeight = 720;

  Window();
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  bool Create(const char* title, std::uint32_t width, std::uint32_t height);

  // Drains pending OS/browser events into the input state.
  void PollEvents();

  // Clears per-frame input; called at the end of every Engine::Frame.
  void EndFrame();

  void RequestPointerLock();
  void ReleasePointerLock();

  // Records a resize for the engine to pick up via ConsumeResize. Used by the
  // web bridge, where the callback arrives outside the frame loop.
  void SetPendingResize(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
    pending_width_ = width;
    pending_height_ = height;
    resize_pending_ = true;
  }

  bool ConsumeResize(std::uint32_t* out_width, std::uint32_t* out_height);

  bool pointer_locked() const { return input_.pointer_locked; }
  bool quit_requested() const { return input_.quit_requested; }

  const InputState& input() const { return input_; }
  InputState& mutable_input() { return input_; }

  std::uint32_t width() const { return width_; }
  std::uint32_t height() const { return height_; }

  // Native only: the platform window handle (HWND on Windows) a GPU surface is
  // built from. nullptr on the web, where the surface comes from the canvas.
  void* native_window_handle() const { return native_handle_; }
  void* native_display_handle() const { return native_display_; }

#ifdef __EMSCRIPTEN__
  // Bridge entry points, called from the page (see window_headless.cpp).
  void ApplyLookDelta(float dx, float dy);
  void SetPointerLocked(bool locked);
  void SetKey(Key key, bool down);

  // Analog movement from an on-screen stick. forward/strafe are -1..1; the
  // caller is expected to pass small values near the centre rather than a
  // snapped deadzone, so the player can creep.
  void SetStick(float forward, float strafe);

  // Releases the stick and hands the axes back to the keyboard.
  void ClearStick();

  // Right-stick look deflection, -1..1 per axis. active distinguishes a
  // centred-but-held stick from a released one, the same way SetStick does.
  void SetLookStick(float x, float y);
  void ClearLookStick();
#endif

 private:
  void UpdateMoveAxes();

  InputState input_;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool resize_pending_ = false;
  std::uint32_t pending_width_ = 0;
  std::uint32_t pending_height_ = 0;

  void* native_handle_ = nullptr;
  void* native_display_ = nullptr;
  void* impl_ = nullptr;  // SDL_Window* when built with SDL
};

#ifdef __EMSCRIPTEN__
// Maps a DOM KeyboardEvent.code to a Key.
Key KeyFromWebCode(const char* code);
#endif

}  // namespace museum::platform
