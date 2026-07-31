#pragma once

#include "graphics/mesh.h"

#include <cstdint>
#include <string>

namespace tamias {

struct MeshAsset {
  std::uint64_t id = 0;
  std::string name;
  MeshCpu cpu;
  std::uint64_t gpu_mesh_id = 0;
};

}  // namespace tamias
