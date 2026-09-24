#include "world/sky.h"

#include <cmath>

#include "core/math.h"

namespace museum::world {

SkyMesh BuildSkyDome(float radius, int segments, int rings) {
  SkyMesh mesh;

  // Sphere around the origin: the shader only uses the direction, so the dome
  // is never translated and the world's own position is irrelevant.
  for (int ring = 0; ring <= rings; ++ring) {
    const float v = static_cast<float>(ring) / static_cast<float>(rings);
    const float phi = v * math::kPi;  // 0 at the top, PI at the bottom
    const float y = std::cos(phi);
    const float r = std::sin(phi);

    for (int segment = 0; segment <= segments; ++segment) {
      const float u = static_cast<float>(segment) / static_cast<float>(segments);
      const float theta = u * math::kPi * 2.0f;

      Vertex vertex;
      vertex.px = r * std::cos(theta) * radius;
      vertex.py = y * radius;
      vertex.pz = r * std::sin(theta) * radius;
      // Normals point inward; the shader ignores them anyway.
      vertex.nx = -r * std::cos(theta);
      vertex.ny = -y;
      vertex.nz = -r * std::sin(theta);
      vertex.u = u;
      vertex.v = v;
      vertex.is_floor = 0.0f;
      mesh.vertices.push_back(vertex);
    }
  }

  const std::uint32_t stride = static_cast<std::uint32_t>(segments) + 1;
  for (int ring = 0; ring < rings; ++ring) {
    for (int segment = 0; segment < segments; ++segment) {
      const std::uint32_t a = static_cast<std::uint32_t>(ring) * stride + segment;
      const std::uint32_t b = a + stride;

      // Wind so the inside of the sphere is front-facing, since only the
      // interior is ever seen.
      mesh.indices.push_back(a);
      mesh.indices.push_back(b);
      mesh.indices.push_back(a + 1);
      mesh.indices.push_back(a + 1);
      mesh.indices.push_back(b);
      mesh.indices.push_back(b + 1);
    }
  }

  return mesh;
}

}  // namespace museum::world
