// Time, in one place. Uses SDL3's high-resolution counter so the fixed-step
// simulation and the render loop never disagree about how much time passed,
// and clamps the frame delta so a hitch cannot teleport the player.
#pragma once

#ifdef MUSEUM_USE_SDL
#include <SDL3/SDL_timer.h>
#else
#include <chrono>
#endif

namespace museum::core {

class Clock {
 public:
  Clock() : start_(NowSeconds()), previous_(start_) {}

  // Advances the clock and returns the clamped frame delta in seconds.
  float Tick() {
    const double now = NowSeconds();
    double delta = now - previous_;
    previous_ = now;

    if (delta > kMaxFrameDelta) {
      delta = kMaxFrameDelta;
    }
    if (delta < 0.0) {
      delta = 0.0;
    }
    frame_delta_ = static_cast<float>(delta);
    elapsed_ = static_cast<float>(now - start_);
    return frame_delta_;
  }

  float frame_delta() const { return frame_delta_; }
  float elapsed() const { return elapsed_; }

 private:
  static constexpr double kMaxFrameDelta = 0.05;  // 20 FPS floor

  static double NowSeconds() {
#ifdef MUSEUM_USE_SDL
    return static_cast<double>(SDL_GetPerformanceCounter()) /
           static_cast<double>(SDL_GetPerformanceFrequency());
#else
    using ClockT = std::chrono::steady_clock;
    return std::chrono::duration<double>(ClockT::now().time_since_epoch()).count();
#endif
  }

  double start_;
  double previous_;
  float frame_delta_ = 0.0f;
  float elapsed_ = 0.0f;
};

}  // namespace museum::core
