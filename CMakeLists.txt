cmake_minimum_required(VERSION 3.20)
project(museum_cpp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(museum src/main.cpp)

if(EMSCRIPTEN)
  # emdawnwebgpu provides <webgpu/webgpu_cpp.h> and links the WebGPU bindings.
  # It replaces the older -sUSE_WEBGPU flag (Emscripten 4.0.10+).
  target_compile_options(museum PRIVATE --use-port=emdawnwebgpu)
  target_link_options(museum PRIVATE
    --use-port=emdawnwebgpu
    -sALLOW_MEMORY_GROWTH=1
    -sEXIT_RUNTIME=0
    --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
  )
  # Emit a runnable page instead of a bare .js.
  set_target_properties(museum PROPERTIES SUFFIX ".html")
else()
  message(FATAL_ERROR
    "Native target is not wired yet (Batch 1). It needs SDL3 plus a WebGPU "
    "implementation (Dawn or wgpu-native). Configure the web build with "
    "emcmake instead.")
endif()
