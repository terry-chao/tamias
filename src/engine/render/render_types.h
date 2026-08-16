#pragma once

#include "engine/math/math.h"

#include <cstdint>

namespace tamias {

enum class RenderMode { Wireframe, Shaded, Realistic };

// 语义侧 → 渲染侧的 draw item。语义层（Document）产出它，渲染侧消费它；
// app 从 Document::render_items() 拿，不再直接碰 SceneNode。
struct SceneDrawItem {
  std::uint64_t node_id = 0;
  std::uint64_t mesh_asset_id = 0;      // 语义几何引用，渲染侧映射到 GPU
  Mat4 transform = Mat4::identity();    // world transform
  Vec3 color{0.75f, 0.78f, 0.82f};      // 解析后的 base_color
  float roughness = 0.6f;               // PBR：粗糙度
  float metallic = 0.0f;                // PBR：金属度
  std::uint64_t albedo_texture_id = 0;  // 0 = 无贴图
  std::uint64_t normal_texture_id = 0;
  bool selected = false;
};

}  // namespace tamias
