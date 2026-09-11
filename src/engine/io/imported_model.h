#pragma once

#include "engine/graphics/mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tamias {

// 网格导入结果：几何 + 可选材质/贴图引用。
// 路径给 OBJ 外链图；bytes 给 GLB 内嵌 png/jpeg（仍由 app 用 QImage 解码）。
struct ImportedModel {
  MeshCpu mesh;
  Vec3 base_color{0.75f, 0.78f, 0.82f};
  float roughness = 0.6f;
  float metallic = 0.0f;
  float opacity = 1.0f;
  std::string albedo_path;
  std::string normal_path;
  std::string orm_path;
  std::vector<std::uint8_t> albedo_bytes;
  std::vector<std::uint8_t> normal_bytes;
  std::vector<std::uint8_t> orm_bytes;
};

}  // namespace tamias
