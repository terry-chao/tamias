#pragma once

#include "engine/math/math.h"
#include "engine/render/texture_asset.h"
#include "engine/render/texture_transform.h"

#include <cstdint>
#include <string>

namespace tamias {

// PBR metallic-roughness 材质。纯数据（无 OCCT/Qt 依赖），可序列化进 .tdoc。
// 纹理 id 为 0 表示「无贴图」：无 albedo 时用 base_color；无法线时 shader 用几何法线。
struct Material {
  std::uint64_t id = 0;
  std::string name;
  Vec3 base_color{0.75f, 0.78f, 0.82f};
  float roughness = 0.6f;
  float metallic = 0.0f;
  float opacity = 1.0f;                 // <1 = 真实感半透明（玻璃）
  std::uint64_t albedo_texture_id = 0;  // 0 = 无
  std::uint64_t normal_texture_id = 0;
  std::uint64_t orm_texture_id = 0;  // packed AO / roughness / metallic；0 = 用标量
  TextureTransform tex;
};

}  // namespace tamias
