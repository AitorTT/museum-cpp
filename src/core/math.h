// Minimal 3D math: vectors, column-major 4x4 matrices, and a camera.
// Deliberately hand-rolled instead of pulling in GLM: this project is about
// control over the hot path, and these few operations are all the camera
// needs. Everything is right-handed, looking down -Z (WebGPU/three.js
// convention), unlike Godot's -Z-forward/+Y-up via a yaw/pitch rig.
#pragma once

#include <cmath>
#include <cstdint>

namespace museum::math {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

inline float Radians(float degrees) { return degrees * kDeg2Rad; }

inline float Clamp(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3 operator-() const { return {-x, -y, -z}; }

  Vec3& operator+=(const Vec3& o) {
    x += o.x;
    y += o.y;
    z += o.z;
    return *this;
  }
  Vec3& operator-=(const Vec3& o) {
    x -= o.x;
    y -= o.y;
    z -= o.z;
    return *this;
  }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

inline float Dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 Cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }

inline Vec3 Normalize(const Vec3& v) {
  const float len = Length(v);
  return len > 0.0f ? v * (1.0f / len) : Vec3{};
}

inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// Column-major 4x4, laid out exactly as WebGPU/WGSL and WGSL's mat4x4<f32>
// expect when uploaded to a uniform buffer (data[column * 4 + row]).
struct Mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  static Mat4 Identity() { return Mat4{}; }

  static Mat4 Translation(const Vec3& t) {
    Mat4 r;
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
  }

  static Mat4 Scale(const Vec3& s) {
    Mat4 r;
    r.m[0] = s.x;
    r.m[5] = s.y;
    r.m[10] = s.z;
    return r;
  }

  // Rotation about an arbitrary unit axis (Rodrigues), used for the yaw/pitch
  // rig and, later, for turntable rotation of sculptures.
  static Mat4 RotationAxis(const Vec3& axis, float radians) {
    const Vec3 a = Normalize(axis);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float t = 1.0f - c;

    Mat4 r;
    r.m[0] = t * a.x * a.x + c;
    r.m[1] = t * a.x * a.y + s * a.z;
    r.m[2] = t * a.x * a.z - s * a.y;

    r.m[4] = t * a.x * a.y - s * a.z;
    r.m[5] = t * a.y * a.y + c;
    r.m[6] = t * a.y * a.z + s * a.x;

    r.m[8] = t * a.x * a.z + s * a.y;
    r.m[9] = t * a.y * a.z - s * a.x;
    r.m[10] = t * a.z * a.z + c;
    return r;
  }

  static Mat4 Perspective(float fov_y_radians, float aspect, float znear, float zfar) {
    // WebGPU depth range is [0, 1]; this is the D3D-style projection.
    const float f = 1.0f / std::tan(fov_y_radians * 0.5f);
    const float nf = 1.0f / (znear - zfar);

    Mat4 r;
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = zfar * nf;
    r.m[11] = -1.0f;
    r.m[14] = znear * zfar * nf;
    r.m[15] = 0.0f;
    return r;
  }

  static Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
      for (int row = 0; row < 4; ++row) {
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) {
          sum += a.m[k * 4 + row] * b.m[col * 4 + k];
        }
        r.m[col * 4 + row] = sum;
      }
    }
    return r;
  }

  // Full 4x4 inverse (projection matrices are not affine, so this is the
  // general case rather than a transpose-shortcut inverse).
  Mat4 Inverse() const {
    const float* a = m;
    float inv[16];

    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
             a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
             a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
             a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
              a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
             a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
             a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
             a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
              a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] +
             a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
             a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
              a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
              a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
             a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] +
             a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] -
              a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] +
              a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (det == 0.0f) {
      return Mat4::Identity();
    }
    det = 1.0f / det;

    Mat4 r;
    for (int i = 0; i < 16; ++i) {
      r.m[i] = inv[i] * det;
    }
    return r;
  }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) { return Mat4::Multiply(a, b); }

// Where the player wants to move, in camera-relative terms. Keyboard, an
// on-screen stick, or a gamepad all write here, so the camera never needs to
// know which one is driving it.
struct MoveInput {
  float forward = 0.0f;  // -1..1, +1 is into the screen
  float strafe = 0.0f;   // -1..1, +1 is to the right
};

}  // namespace museum::math
