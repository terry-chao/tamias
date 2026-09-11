#pragma once

#include "engine/core/result.h"
#include "engine/graphics/mesh.h"
#include "engine/render/material.h"
#include "engine/render/render_types.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace tamias {

// CPU 侧渲染快照：被引用的 MeshCpu + 已烘好的 SceneDrawItem + 相机。
// 不是 .tdoc（无实体/特征），也不是 RenderDoc（无 GPU 命令）。
struct RenderScene {
  struct View {
    Mat4 view = Mat4::identity();
    Mat4 proj = Mat4::identity();
    Vec3 eye_position{};
    Vec3 target{};
    float view_distance = 5.f;
    float yaw = 0.785398163f;
    float pitch = 0.35f;
    float fovy = 0.8f;
    float znear = 0.05f;
    float zfar = 500.f;
    RenderMode mode = RenderMode::Shaded;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    bool orthographic = false;
  };

  std::uint32_t version = 3;
  std::string source;
  View view;
  std::unordered_map<std::uint64_t, MeshCpu> meshes;
  std::unordered_map<std::uint64_t, TextureAsset> textures;
  std::vector<SceneDrawItem> items;
  // 捕获时视口隐藏的语义节点（楼层 / 类别 / isolate）。旧文件缺省为空 = 全显示。
  std::vector<std::uint64_t> hidden_node_ids;
};

// 只保留被 items 引用且能在 meshes 里找到的条目；贴图只保留 albedo/normal/ORM 引用到的。
// items 按 node_id 排序。
RenderScene bake_render_scene(
    std::vector<SceneDrawItem> items, const std::unordered_map<std::uint64_t, MeshCpu>& meshes,
    RenderScene::View view, std::string source,
    const std::unordered_map<std::uint64_t, TextureAsset>& textures = {});

[[nodiscard]] std::uint64_t render_scene_triangle_count(const RenderScene& scene);
[[nodiscard]] std::string render_scene_digest(const RenderScene& scene);
// 人读全量 dump：相机矩阵、每条 draw、全部顶点/索引、贴图像素摘要。
[[nodiscard]] std::string inspect_render_scene(const RenderScene& scene);

// 写出 inspect 文本 + debug/ 可视化 sidecar（mesh_*.obj、draw_*_world.obj、tex_*.ppm）。
Result<void> write_render_scene_debug_files(const std::filesystem::path& inspect_path,
                                            const std::filesystem::path& debug_dir,
                                            const RenderScene& scene);
// 紧挨着 .trscn：stem.inspect.txt 与 stem.debug/。
Result<void> write_render_scene_debug_sidecars(const std::filesystem::path& trscn_path,
                                               const RenderScene& scene);
// 单条 draw 的世界空间 OBJ（含 UV / 法线），方便只丢这一条进 DCC。
Result<void> write_render_scene_debug_draw(const std::filesystem::path& obj_path,
                                           const RenderScene& scene, std::size_t item_index);

Result<std::vector<std::uint8_t>> serialize_render_scene(const RenderScene& scene);
Result<RenderScene> deserialize_render_scene(std::span<const std::uint8_t> bytes);
Result<void> save_render_scene(const std::filesystem::path& path, const RenderScene& scene);
Result<RenderScene> load_render_scene(const std::filesystem::path& path);
[[nodiscard]] bool is_render_scene_path(const std::filesystem::path& path);

}  // namespace tamias
