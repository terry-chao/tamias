#pragma once

#include "engine/render/rhi/device.h"

#include <cstdint>
#include <functional>

namespace tamias {

// Draw grouping key. Transform / color / selected stay on the instance, not here.
// See docs/INSTANCING.md.
struct BatchKey {
  std::uint64_t gpu_mesh_id = 0;
  PipelineState* pipeline = nullptr;
  Texture* albedo = nullptr;
  Texture* normal = nullptr;
  bool lines = false;
  bool transparent = false;

  [[nodiscard]] bool operator==(const BatchKey& o) const {
    return gpu_mesh_id == o.gpu_mesh_id && pipeline == o.pipeline && albedo == o.albedo &&
           normal == o.normal && lines == o.lines && transparent == o.transparent;
  }
};

struct BatchKeyHash {
  std::size_t operator()(const BatchKey& k) const {
    std::size_t h = static_cast<std::size_t>(k.gpu_mesh_id);
    h ^= std::hash<const void*>{}(k.pipeline) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<const void*>{}(k.albedo) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<const void*>{}(k.normal) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(k.lines) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(k.transparent) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

}  // namespace tamias
