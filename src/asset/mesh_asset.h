#pragma once

#include "graphics/mesh.h"

#include <cstdint>
#include <string>

namespace tamias {

// Semantic-side geometry asset: CPU mesh only. GPU resources live on the render
// side (see render_runtime.h); the semantic layer refers to geometry by asset id.
struct MeshAsset {
  std::uint64_t id = 0;
  std::string name;
  MeshCpu cpu;
};

}  // namespace tamias
