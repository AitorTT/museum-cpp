# Museum (C++)

A first-person 3D museum, written in C++ against `webgpu.h`. One source
builds for the **web** (Emscripten + emdawnwebgpu) and, from Batch 1, for
**native** (Dawn or wgpu-native).

This is the performance-focused successor to the Godot museum and the
Three.js `museum-js` port. Goal: better rendering, lighting and sculptures.

## Status

- **Batch 0 (done):** toolchain, CMake, and a minimal WebGPU triangle that
  builds and runs in the browser. Native target is scaffolded in
  `CMakeLists.txt` but not wired yet.
- **Batch 1 (next):** SDL3 window/input, math, player/camera, resize.

## Prerequisites

- [Emscripten](https://emscripten.org) 4.0.10+ (tested on 6.0.10).
  `emsdk install latest && emsdk activate latest`.
- CMake 3.20+, Ninja.

## Build (web)

```sh
# from an environment where emsdk is active (emsdk_env)
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```

Output: `build-web/museum.html` (+ `.js`, `.wasm`).

## Run

WASM needs a real HTTP origin (`file://` will not work):

```sh
python -m http.server 8000 --directory build-web
```

Then open <http://localhost:8000/museum.html> in Chrome or Edge. You should
see an orange triangle on a dark background, and in the console:
`Museum: device ready, surface <w>x<h>`.

## Layout

```
CMakeLists.txt      build config for web (and native, later)
web/shell.html      page hosting the canvas (#canvas)
src/main.cpp        entry point: instance, adapter, device, surface, frame loop
```
