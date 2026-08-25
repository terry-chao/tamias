#pragma once

#include "engine/math/math.h"
#include "engine/render/rhi/device.h"

#include <cstdint>
#include <memory>

namespace tamias {

enum class RenderMode { Wireframe, Shaded, Realistic };

// 语义侧 → 渲染侧的 draw item。语义层（Document）产出它，渲染侧消费它；
// app 从 Document::render_items() 拿，不再直接碰 SceneNode。
struct SceneDrawItem {
  std::uint64_t node_id = 0;
  std::uint64_t mesh_asset_id = 0;      // 语义几何引用，渲染侧映射到 GPU
  Mat4 transform = Mat4::identity();    // world transform
  Aabb bounds{};                        // world bounds（录制时视锥剔除用；无效盒不剔除）
  Vec3 color{0.75f, 0.78f, 0.82f};      // 解析后的 base_color
  float roughness = 0.6f;               // PBR：粗糙度
  float metallic = 0.0f;                // PBR：金属度
  std::uint64_t albedo_texture_id = 0;  // 0 = 无贴图
  std::uint64_t normal_texture_id = 0;
  bool selected = false;
  bool lines = false;  // 草图折线：用 LineList 画，而不是三角面
};

// GPU 网格资源（渲染线程留存缓存 meshes_ 的条目）。
struct GpuMesh {
  std::unique_ptr<Buffer> vertex_buffer;
  std::unique_ptr<Buffer> index_buffer;
  std::uint32_t index_count = 0;
  Aabb bounds{};
  bool line_list = false;
};

struct GpuTexture {
  std::unique_ptr<Texture> texture;
};

// 与 shader 的 push constants 布局一一对应（std140）。
struct PushConstants {
  Mat4 mvp;
  Mat4 model;
  float color[4];
  float material[4];  // x=roughness, y=metallic, z=has_albedo, w=has_normal
  float light_dir_selected[4];
  float eye_pos_mode[4];
};

}  // namespace tamias
