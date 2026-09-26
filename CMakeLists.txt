cmake_minimum_required(VERSION 3.20)
project(museum_cpp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Release with symbols; a museum is a long-running interactive app, so a crash
# in a release build is the interesting one to be able to read.
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g -DNDEBUG")

set(MUSEUM_SOURCES
  src/main.cpp
  src/engine.cpp
  src/core/clock.h
  src/core/math.h
  src/platform/window.h
  src/player/player.cpp
  src/render/renderer.cpp
  src/render/shaders.h
  src/render/gltf.cpp
  src/render/gltf.h
  src/render/painting_atlas.cpp
  src/render/painting_atlas.h
  src/render/texture.cpp
  src/render/texture.h
  src/world/config.h
  src/world/layout.cpp
  src/world/layout.h
  src/world/room_builder.cpp
  src/world/room_builder.h
  src/world/collision.h
  src/world/lights.h
  src/world/museum.cpp
  src/world/museum.h
  src/world/shadows.cpp
  src/world/shadows.h
  src/world/sky.cpp
  src/world/sky.h
)

# Where the prebuilt dependencies land when configured the easy way on Windows.
# Both are optional: point CMAKE_PREFIX_PATH at them, or drop them in .deps/.
set(MUSEUM_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps" CACHE PATH
    "Directory holding prebuilt Dawn/SDL3 packages")

if(EMSCRIPTEN)
  # --------------------------------------------------------------------------
  # Web: Emscripten + emdawnwebgpu, which provides <webgpu/webgpu_cpp.h>.
  # Replaces the older -sUSE_WEBGPU flag (Emscripten 4.0.10+).
  # --------------------------------------------------------------------------
  add_executable(museum ${MUSEUM_SOURCES} src/platform/window_headless.cpp)
  target_compile_definitions(museum PRIVATE MUSEUM_WEB)
  target_include_directories(museum PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

  target_compile_options(museum PRIVATE --use-port=emdawnwebgpu)
  target_link_options(museum PRIVATE
    --use-port=emdawnwebgpu
    -sALLOW_MEMORY_GROWTH=1
    -sEXIT_RUNTIME=0
    -sASYNCIFY=1                 # lets Initialize await adapter/device
    -sASYNCIFY_IMPORTS=["emscripten_sleep"]
    # Package the assets tree into the virtual filesystem at /assets, so
    # stb_image can fopen them the same way it does natively.
    --preload-file "${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
  )
  set_target_properties(museum PROPERTIES SUFFIX ".html")

else()
  # --------------------------------------------------------------------------
  # Native: SDL3 + a WebGPU implementation (Dawn or wgpu-native).
  # --------------------------------------------------------------------------
  add_executable(museum ${MUSEUM_SOURCES} src/platform/window_sdl.cpp)
  target_compile_definitions(museum PRIVATE MUSEUM_USE_SDL)
  target_include_directories(museum PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

  find_package(SDL3 CONFIG QUIET
    HINTS "${MUSEUM_DEPS_DIR}/sdl3-mingw/SDL3/x86_64-w64-mingw32")
  if(NOT SDL3_FOUND)
    message(FATAL_ERROR
      "SDL3 not found. Download SDL3-devel-*-mingw.zip into .deps/sdl3-mingw/ "
      "(see README) or set CMAKE_PREFIX_PATH.")
  endif()
  target_link_libraries(museum PRIVATE SDL3::SDL3)

  # Dawn ships a CMake package and a static webgpu_dawn library. Its exported
  # target carries MSVC-style flags (-ignore:4221, user32.lib) that MinGW's
  # linker rejects, so import it as a plain imported target instead of using
  # the package's link interface.
  set(_dawn_dir "${MUSEUM_DEPS_DIR}/dawn")
  if(NOT EXISTS "${_dawn_dir}/include/webgpu/webgpu_cpp.h")
    message(FATAL_ERROR
      "Dawn not found at ${_dawn_dir}. Download a Dawn windows-latest-Release "
      "build and extract it there (see README).")
  endif()

  add_library(dawn_webgpu STATIC IMPORTED GLOBAL)
  set_target_properties(dawn_webgpu PROPERTIES
    IMPORTED_LOCATION "${_dawn_dir}/lib/webgpu_dawn.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${_dawn_dir}/include")
  target_link_libraries(museum PRIVATE dawn_webgpu)

  # Dawn's static library needs the platform graphics + threading libraries.
  if(WIN32)
    target_link_libraries(museum PRIVATE d3d12 dxgi dxguid user32 gdi32
                                       ole32 windowscodecs onecore_apiset
                                       dbghelp)
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(museum PRIVATE Threads::Threads)
endif()
