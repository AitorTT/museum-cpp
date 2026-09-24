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
| 2 | Layout + RoomBuilder port, merged static geometry, AABB collision | done |
| 3 | Renderer: materials, textures, sky, tonemapping | next |

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
    player.{h,cpp}    yaw/pitch rig, AABB movement, gravity, view matrices
  render/
    renderer.{h,cpp}  device bring-up, surface, depth target, frame
  world/
    config.h          room/door/window dimensions, FloorTopY
    layout.{h,cpp}    the four floors, door bits, SealVoidDoors()
    room_builder.{h,cpp}  wall segmentation with door and window cuts
    collision.h       per-axis AABB resolve (the CharacterBody3D replacement)
    museum.{h,cpp}    builds every room, corner patches, and the merged mesh
web/
  shell.html          page, canvas, pointer lock, input bridge
batch2_check.mjs      Playwright check: counts, spawn, eye height, collision
```

## World model

The museum is 4 floors of 10u rooms on a grid, 43 rooms total, matching the JS
and Godot builds exactly. Every piece of geometry is an axis-aligned box, and
each box is also a collider, so the collision set is generated from the same
data that is drawn rather than derived from the mesh.

Collision is per-axis (X, Z, then Y) with a minimum-translation resolve. Two
details are carried over deliberately because removing either breaks movement:

- On Y, floors resolve **last**, so a wall's downward push cannot override the
  floor supporting the player (that ordering causes an order-dependent death
  spiral).
- A resolve only moves along the current pass's axis when that axis is the box's
  minimal-penetration axis, which is what stops a full-height wall from shoving
  the player through the floor while they walk beside it.

Movement is substepped so no slice exceeds 0.08 m, well under the thinnest
collider (0.15 m slabs and walls), so nothing can be tunnelled through at any
speed.

`room_builder.cpp` reproduces a precedence quirk from the JS source
(`doorXP ? 1 : 0 + ...` binds as `doorXP ? 1 : (0 + ...)`), so painting
placement matches the JS museum, which in turn matched Godot.

Verified against the JS museum's own `smoke.mjs` expectations: 43 rooms, spawn
at (-3.129, -8.900), and eye height 1.769 m above the feet.

Constants in `player/player.h` and `world/config.h` are ported from the JS
museum's `config.ts`, which in turn ported them from Godot's
`character_body_3d.gd`, so movement and spawn match across all three museums.

## Testing

`batch2_check.mjs` boots the web build in headless Edge via Playwright and
checks the build counts, spawn point, eye height, walking, and wall collision:

```sh
npm install --no-save playwright
node batch2_check.mjs        # expects the build served on :8000
```
