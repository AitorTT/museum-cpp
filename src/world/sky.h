// The sky dome: an inward-facing sphere the fragment shader turns into a
// vertical gradient. Replaces Godot's flat grey clear colour, matching the JS
// museum's Sky.ts.
#pragma once

#include <cstdint>
#include <vector>

#include "world/museum.h"

namespace museum::world {

// The sky is just another mesh: the renderer uploads it the same way, the
// shader simply ignores normals and UVs.
using SkyMesh = Mesh;

// A low-poly sphere (the gradient needs no detail) large enough that the world
// never pokes through it.
SkyMesh BuildSkyDome(float radius = 2000.0f, int segments = 32, int rings = 16);

}  // namespace museum::world
