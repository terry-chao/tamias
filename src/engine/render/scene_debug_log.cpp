#include "engine/render/scene_debug_log.h"

#include "engine/render/recording_command_list.h"
#include "engine/render/scene_graph.h"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace tamias {
namespace {

class MockBuffer final : public Buffer {
 public:
  const BufferDesc& desc() const override { return desc_; }
  Result<void> write(std::uint64_t, std::span<const std::byte>) override { return {}; }
  BufferDesc desc_{};
};

class MockTexture final : public Texture {
 public:
  const TextureDesc& desc() const override { return desc_; }
  Result<void> write(std::uint64_t, std::span<const std::byte>) override { return {}; }
  Result<void> write_subresource(std::uint32_t, std::uint32_t,
                                 std::span<const std::byte>) override {
    return {};
  }
  TextureDesc desc_{};
};

class MockPipeline final : public PipelineState {};

std::uint32_t item_triangles(const RenderScene& scene, const SceneDrawItem& item) {
  const auto it = scene.meshes.find(item.mesh_asset_id);
  if (it == scene.meshes.end() || it->second.line_list) {
    return 0;
  }
  return static_cast<std::uint32_t>(it->second.indices.size() / 3);
}

}  // namespace

const char* scene_debug_skip_reason_name(SceneDebugSkipReason reason) {
  switch (reason) {
    case SceneDebugSkipReason::Drawn:
      return "drawn";
    case SceneDebugSkipReason::Hidden:
      return "hidden";
    case SceneDebugSkipReason::Isolated:
      return "isolated";
    case SceneDebugSkipReason::Stepped:
      return "stepped";
    case SceneDebugSkipReason::Culled:
      return "culled";
  }
  return "unknown";
}

SceneDebugLog capture_scene_debug_log(const SceneDebugPlayer& player, const Frustum* frustum) {
  SceneDebugLog log;
  if (!player.has_scene()) {
    return log;
  }
  const RenderScene& scene = player.scene();
  const std::vector<SceneDrawItem> submitted = player.filtered_items();
  std::unordered_set<std::uint64_t> submitted_nodes;
  submitted_nodes.reserve(submitted.size());
  for (const auto& item : submitted) {
    submitted_nodes.insert(item.node_id);
  }
  const std::vector<std::uint64_t> hidden_ids = player.hidden_node_ids();
  std::unordered_set<std::uint64_t> hidden(hidden_ids.begin(), hidden_ids.end());

  log.items.reserve(scene.items.size());
  for (std::size_t i = 0; i < scene.items.size(); ++i) {
    const SceneDrawItem& item = scene.items[i];
    SceneDebugLog::Item row;
    row.index = i;
    row.node_id = item.node_id;
    row.mesh_asset_id = item.mesh_asset_id;
    row.triangles = item_triangles(scene, item);
    if (player.isolate_node() && item.node_id != *player.isolate_node()) {
      row.reason = SceneDebugSkipReason::Isolated;
      ++log.isolated;
    } else if (submitted_nodes.count(item.node_id) == 0) {
      row.reason = SceneDebugSkipReason::Stepped;
      ++log.stepped;
    } else if (hidden.count(item.node_id) != 0) {
      row.reason = SceneDebugSkipReason::Hidden;
      ++log.hidden;
    } else if (frustum != nullptr && item.bounds.valid() && !frustum->intersects(item.bounds)) {
      row.reason = SceneDebugSkipReason::Culled;
      ++log.culled;
    } else {
      row.reason = SceneDebugSkipReason::Drawn;
      ++log.drawn_items;
    }
    log.items.push_back(row);
  }

  RecordingCommandList cmds;
  MockPipeline shaded;
  MockPipeline wire;
  MockPipeline entity_line;
  MockPipeline blend;
  MockTexture default_tex;
  MockTexture default_normal;
  MockTexture default_orm;
  MockBuffer instance_buf;
  std::unordered_map<std::uint64_t, GpuMesh> meshes;
  std::unordered_map<std::uint64_t, std::uint64_t> asset_to_gpu;
  std::unordered_map<std::uint64_t, GpuTexture> textures;
  std::unordered_map<std::uint64_t, std::uint64_t> texture_asset_to_gpu;
  bool texture_diag_logged = false;

  for (const auto& [id, cpu] : scene.meshes) {
    GpuMesh mesh;
    mesh.vertex_buffer = std::make_unique<MockBuffer>();
    mesh.index_buffer = std::make_unique<MockBuffer>();
    mesh.index_count = static_cast<std::uint32_t>(cpu.indices.size());
    mesh.line_list = cpu.line_list;
    mesh.has_texcoord = cpu.has_texcoord;
    mesh.bounds = cpu.bounds;
    const std::uint64_t gpu_id = meshes.size() + 1;
    meshes.emplace(gpu_id, std::move(mesh));
    asset_to_gpu[id] = gpu_id;
  }
  std::uint64_t gpu_tex = 1;
  for (const auto& item : submitted) {
    for (std::uint64_t tex_id :
         {item.albedo_texture_id, item.normal_texture_id, item.orm_texture_id}) {
      if (tex_id == 0 || texture_asset_to_gpu.contains(tex_id)) {
        continue;
      }
      GpuTexture gpu;
      gpu.texture = std::make_unique<MockTexture>();
      textures.emplace(gpu_tex, std::move(gpu));
      texture_asset_to_gpu[tex_id] = gpu_tex;
      ++gpu_tex;
    }
  }

  const Mat4 view_proj = Mat4::identity();
  SceneGraphDrawContext ctx{};
  ctx.command_list = &cmds;
  ctx.view_proj = &view_proj;
  ctx.frustum = frustum;
  ctx.hidden_nodes = &hidden;
  ctx.eye_position = scene.view.eye_position;
  ctx.fovy = scene.view.fovy;
  ctx.framebuffer_height = static_cast<float>(std::max(1u, scene.view.height));
  ctx.mode_value = scene.view.mode == RenderMode::Wireframe    ? 0.f
                   : scene.view.mode == RenderMode::Realistic ? 2.f
                                                              : 1.f;
  ctx.shaded_pipeline = &shaded;
  ctx.wire_pipeline = &wire;
  ctx.entity_line_pipeline = &entity_line;
  ctx.blend_pipeline = &blend;
  ctx.default_texture = &default_tex;
  ctx.default_normal = &default_normal;
  ctx.default_orm = &default_orm;
  ctx.meshes = &meshes;
  ctx.asset_to_gpu = &asset_to_gpu;
  ctx.textures = &textures;
  ctx.texture_asset_to_gpu = &texture_asset_to_gpu;
  ctx.texture_diag_logged = &texture_diag_logged;
  ctx.instance_buffer = &instance_buf;

  auto root = build_scene_graph(submitted);
  if (root) {
    {
      RecordCommands visitor(ctx);
      root->accept(visitor);
    }
    const std::size_t opaque_end = cmds.draws.size();
    if (ctx.mode_value > 1.5f) {
      ctx.transparent_pass = true;
      RecordCommands trans(ctx);
      root->accept(trans);
    }
    log.draws.reserve(cmds.draws.size());
    for (std::size_t i = 0; i < cmds.draws.size(); ++i) {
      const auto& d = cmds.draws[i];
      SceneDebugLog::Draw row;
      row.index = i;
      row.index_count = d.desc.index_count;
      row.instance_count = d.desc.instance_count;
      row.transparent = i >= opaque_end;
      row.lines = d.pipeline == &entity_line || d.pipeline == &wire;
      if (d.pipeline == &blend) {
        row.pipeline = "transparent";
      } else if (d.pipeline == &entity_line) {
        row.pipeline = "lines";
      } else if (d.pipeline == &wire) {
        row.pipeline = "wire";
      } else {
        row.pipeline = "shaded";
      }
      log.draws.push_back(std::move(row));
    }
  }
  return log;
}

}  // namespace tamias
