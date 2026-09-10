#pragma once

#include "engine/graphics/mesh.h"

#include <cstdint>
#include <string>

namespace tamias {

// Semantic-side geometry asset: CPU mesh only. GPU resources live on the render
// side (see render_runtime.h); the semantic layer refers to geometry by asset id.
// Identical tessellations share one asset (G1a intern, see docs/INSTANCING.md).
struct MeshAsset {
  std::uint64_t id = 0;
  std::string name;
  MeshCpu cpu;
  std::uint64_t content_hash = 0;
};

}  // namespace tamias
