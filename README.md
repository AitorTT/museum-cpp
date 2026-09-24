# Museum (C++)

A first-person 3D museum, written in C++ against `webgpu.h`. One source tree
builds for the **web** (Emscripten + emdawnwebgpu) and for **native** desktop
(SDL3 + Dawn).

This is the performance-focused successor to the Godot museum and the Three.js
`museum-js` port. Goal: better rendering, lighting and sculptures.

## Status

| Batch | Scope | State |
|---|---|---|
| 0 | Toolchain, CMake, minimal WebGPU triangle | done |
| 1 | SDL3 window/input, math, player/camera, resize, native target | web done; native blocked (see below) |
| 2 | Layout + RoomBuilder port, AABB collision | next |

### Batch 1 native build blocker

The native target compiles but cannot link with the MinGW toolchain on this
machine. The reason is concrete: **Dawn's prebuilt `webgpu_dawn.lib` is an
MSVC-compiled static library**. It embeds the whole Tint shader compiler as
C++ objects, so its symbols reference the MSVC C++ runtime
(`__CxxFrameHandler4`, `__GSHandlerCheck_EH4`, MSVC-mangled `tint::` symbols).
MinGW's `ld` cannot resolve those.

Options, in order of preference:

1. **MSVC toolchain** (Visual Studio Build Tools). Dawn's prebuilt library is
   built *with and for* MSVC, so this links. It coexists with the existing
   MinGW/WinLibs install; CMake just picks a different generator.
2. **Build Dawn from source** with MinGW. Possible but heavy (Chromium-adjacent,
   long build) and unsupported by upstream.
3. **Use wgpu-native (Rust) instead of Dawn.** Ships a normal C ABI (`wgpu.h`)
   that MinGW can link. Caveat from `carver/OptimizedC++Web.txt`: wgpu-native
   does **not** yet implement the stable `webgpu.h`, so the C++ wrapper code
   needs a translation layer.

The web target is unaffected and remains the primary development path, which is
why the Batches are ordered web-first.

## Dependencies

The web build needs only Emscripten. The native build additionally needs SDL3
and Dawn, both dropped into `.deps/` as prebuilt packages.

```
.deps/
  dawn/                  Dawn windows-latest-Release (include/, lib/)
  sdl3-mingw/SDL3/       SDL3-devel-*-mingw.zip, extracted
```

CMake finds them via `MUSEUM_DEPS_DIR` (defaults to `.deps/`).

## Build (web)

From an environment with emsdk active:

```sh
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```

Output `build-web/museum.html` (+ `.js`, `.wasm`). Serve over HTTP; `file://`
will not work:

```sh
python -m http.server 8000 --directory build-web
```

Open <http://localhost:8000/museum.html>. Click to enter, WASD to move, mouse to
look. You should see a world-locked grid that streams past as you walk.

## Build (native)

```sh
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-native
```

See the blocker above: this needs an MSVC toolchain to link.

## Layout

```
src/
  main.cpp            entry point; the per-platform loop
  engine.{h,cpp}      input -> simulation -> render, once per frame
  core/
    math.h            Vec3, Mat4, camera basis. Hand-rolled, no GLM
    clock.h           hi-res timing with a clamped frame delta
  platform/
    window.h          the portable interface (no Win32, no GPU types)
    window_sdl.cpp    desktop implementation (SDL3)
    window_headless.cpp  web implementation + the JS bridge
  player/
    player.{h,cpp}    yaw/pitch camera rig, movement, gravity, view matrices
  render/
    renderer.{h,cpp}  device bring-up, surface, depth target, frame
web/
  shell.html          page, canvas, pointer lock, input bridge
```

Constants in `player/player.h` are ported from the JS museum's `config.ts`,
which in turn ported them from Godot's `character_body_3d.gd`, so movement and
spawn match across all three museums.
