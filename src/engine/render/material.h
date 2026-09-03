#pragma once

#include "engine/math/math.h"

#include <cstdint>
#include <string>
#include <vector>

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
};

// 已解码的 RGBA8 纹理。引擎侧只存字节，图片解码由 app 层用 QImage 完成，
// 再把字节交给 Document::add_texture —— 引擎保持 Qt-free。
struct TextureAsset {
  std::uint64_t id = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;  // RGBA8，size == width * height * 4
  bool srgb = true;                // false = 线性（法线贴图）
};

}  // namespace tamias
