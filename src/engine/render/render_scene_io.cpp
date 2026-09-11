#include "engine/render/render_scene.h"

#include "engine/core/fs_utf8.h"
#include "engine/io/binary_archive.h"
#include "engine/io/mesh_binary.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <vector>

namespace tamias {
namespace {

constexpr char kMagic[4] = {'T', 'R', 'S', 'C'};
constexpr std::uint32_t kFormatVersion = 3;
constexpr std::uint32_t kMinFormatVersion = 1;
constexpr std::uint32_t kTextureTransformFormatVersion = 2;
constexpr std::uint32_t kOrmTextureFormatVersion = 3;

constexpr std::uint32_t fourcc(char a, char b, char c, char d) {
  return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24);
}

constexpr std::uint32_t kChunkMeta = fourcc('M', 'E', 'T', 'A');
constexpr std::uint32_t kChunkView = fourcc('V', 'I', 'E', 'W');
constexpr std::uint32_t kChunkMesh = fourcc('M', 'E', 'S', 'H');
constexpr std::uint32_t kChunkDraw = fourcc('D', 'R', 'A', 'W');
constexpr std::uint32_t kChunkTex = fourcc('T', 'E', 'X', 'T');
constexpr std::uint32_t kChunkHidn = fourcc('H', 'I', 'D', 'N');
constexpr std::uint32_t kChunkSgrf = fourcc('S', 'G', 'R', 'F');

Result<void> write_vec3(BinaryWriter& w, const Vec3& v) {
  if (auto r = w.write_f32(v.x); !r) {
    return r;
  }
  if (auto r = w.write_f32(v.y); !r) {
    return r;
  }
  return w.write_f32(v.z);
}

Result<void> read_vec3(BinaryReader& r, Vec3& v) {
  auto x = r.read_f32();
  if (!x) {
    return Err(x.error());
  }
  auto y = r.read_f32();
  if (!y) {
    return Err(y.error());
  }
  auto z = r.read_f32();
  if (!z) {
    return Err(z.error());
  }
  v = {*x, *y, *z};
  return {};
}

Result<void> write_mat4(BinaryWriter& w, const Mat4& m) {
  for (float f : m.m) {
    if (auto r = w.write_f32(f); !r) {
      return r;
    }
  }
  return {};
}

Result<void> read_mat4(BinaryReader& r, Mat4& m) {
  for (float& f : m.m) {
    auto v = r.read_f32();
    if (!v) {
      return Err(v.error());
    }
    f = *v;
  }
  return {};
}

Result<void> write_aabb(BinaryWriter& w, const Aabb& box) {
  if (auto r = write_vec3(w, box.min); !r) {
    return r;
  }
  return write_vec3(w, box.max);
}

Result<void> read_aabb(BinaryReader& r, Aabb& box) {
  if (auto res = read_vec3(r, box.min); !res) {
    return res;
  }
  return read_vec3(r, box.max);
}

Result<void> append_chunk(BinaryWriter& file, std::uint32_t id,
                          const std::vector<std::uint8_t>& payload) {
  if (auto r = file.write_u32(id); !r) {
    return r;
  }
  if (auto r = file.write_u64(static_cast<std::uint64_t>(payload.size())); !r) {
    return r;
  }
  if (!payload.empty()) {
    return file.write_bytes(payload.data(), payload.size());
  }
  return {};
}

Result<void> write_view(BinaryWriter& w, const RenderScene::View& view) {
  if (auto r = write_mat4(w, view.view); !r) {
    return r;
  }
  if (auto r = write_mat4(w, view.proj); !r) {
    return r;
  }
  if (auto r = write_vec3(w, view.eye_position); !r) {
    return r;
  }
  if (auto r = write_vec3(w, view.target); !r) {
    return r;
  }
  if (auto r = w.write_f32(view.view_distance); !r) {
    return r;
  }
  if (auto r = w.write_f32(view.yaw); !r) {
    return r;
  }
  if (auto r = w.write_f32(view.pitch); !r) {
    return r;
  }
  if (auto r = w.write_f32(view.fovy); !r) {
    return r;
  }
  if (auto r = w.write_f32(view.znear); !r) {
    return r;
  }
  if (auto r = w.write_f32(view.zfar); !r) {
    return r;
  }
  if (auto r = w.write_u32(static_cast<std::uint32_t>(view.mode)); !r) {
    return r;
  }
  if (auto r = w.write_u32(view.width); !r) {
    return r;
  }
  if (auto r = w.write_u32(view.height); !r) {
    return r;
  }
  return w.write_bool(view.orthographic);
}

Result<void> read_view(BinaryReader& r, RenderScene::View& view) {
  if (auto res = read_mat4(r, view.view); !res) {
    return res;
  }
  if (auto res = read_mat4(r, view.proj); !res) {
    return res;
  }
  if (auto res = read_vec3(r, view.eye_position); !res) {
    return res;
  }
  if (auto res = read_vec3(r, view.target); !res) {
    return res;
  }
  auto distance = r.read_f32();
  if (!distance) {
    return Err(distance.error());
  }
  view.view_distance = *distance;
  auto yaw = r.read_f32();
  if (!yaw) {
    return Err(yaw.error());
  }
  view.yaw = *yaw;
  auto pitch = r.read_f32();
  if (!pitch) {
    return Err(pitch.error());
  }
  view.pitch = *pitch;
  auto fovy = r.read_f32();
  if (!fovy) {
    return Err(fovy.error());
  }
  view.fovy = *fovy;
  auto znear = r.read_f32();
  if (!znear) {
    return Err(znear.error());
  }
  view.znear = *znear;
  auto zfar = r.read_f32();
  if (!zfar) {
    return Err(zfar.error());
  }
  view.zfar = *zfar;
  auto mode = r.read_u32();
  if (!mode) {
    return Err(mode.error());
  }
  view.mode = static_cast<RenderMode>(*mode);
  auto width = r.read_u32();
  if (!width) {
    return Err(width.error());
  }
  view.width = *width;
  auto height = r.read_u32();
  if (!height) {
    return Err(height.error());
  }
  view.height = *height;
  auto ortho = r.read_bool();
  if (!ortho) {
    return Err(ortho.error());
  }
  view.orthographic = *ortho;
  return {};
}

Result<void> write_item(BinaryWriter& w, const SceneDrawItem& item) {
  if (auto r = w.write_u64(item.node_id); !r) {
    return r;
  }
  if (auto r = w.write_u64(item.mesh_asset_id); !r) {
    return r;
  }
  if (auto r = write_mat4(w, item.transform); !r) {
    return r;
  }
  if (auto r = write_aabb(w, item.bounds); !r) {
    return r;
  }
  if (auto r = write_vec3(w, item.color); !r) {
    return r;
  }
  if (auto r = write_vec3(w, item.category_color); !r) {
    return r;
  }
  if (auto r = w.write_f32(item.roughness); !r) {
    return r;
  }
  if (auto r = w.write_f32(item.metallic); !r) {
    return r;
  }
  if (auto r = w.write_f32(item.opacity); !r) {
    return r;
  }
  if (auto r = w.write_u64(item.albedo_texture_id); !r) {
    return r;
  }
  if (auto r = w.write_u64(item.normal_texture_id); !r) {
    return r;
  }
  if (auto r = w.write_bool(item.selected); !r) {
    return r;
  }
  if (auto r = w.write_bool(item.lines); !r) {
    return r;
  }
  if (auto r = w.write_f32(item.tex.scale.x); !r) {
    return r;
  }
  if (auto r = w.write_f32(item.tex.scale.y); !r) {
    return r;
  }
  if (auto r = w.write_f32(item.tex.offset.x); !r) {
    return r;
  }
  if (auto r = w.write_f32(item.tex.offset.y); !r) {
    return r;
  }
  if (auto r = w.write_f32(item.tex.rotation); !r) {
    return r;
  }
  if (auto r = w.write_f32(item.tex.world_scale); !r) {
    return r;
  }
  return w.write_u64(item.orm_texture_id);
}

Result<void> read_item(BinaryReader& r, SceneDrawItem& item, std::uint32_t version) {
  auto node_id = r.read_u64();
  if (!node_id) {
    return Err(node_id.error());
  }
  item.node_id = *node_id;
  auto mesh_id = r.read_u64();
  if (!mesh_id) {
    return Err(mesh_id.error());
  }
  item.mesh_asset_id = *mesh_id;
  if (auto res = read_mat4(r, item.transform); !res) {
    return res;
  }
  if (auto res = read_aabb(r, item.bounds); !res) {
    return res;
  }
  if (auto res = read_vec3(r, item.color); !res) {
    return res;
  }
  if (auto res = read_vec3(r, item.category_color); !res) {
    return res;
  }
  auto roughness = r.read_f32();
  if (!roughness) {
    return Err(roughness.error());
  }
  item.roughness = *roughness;
  auto metallic = r.read_f32();
  if (!metallic) {
    return Err(metallic.error());
  }
  item.metallic = *metallic;
  auto opacity = r.read_f32();
  if (!opacity) {
    return Err(opacity.error());
  }
  item.opacity = *opacity;
  auto albedo = r.read_u64();
  if (!albedo) {
    return Err(albedo.error());
  }
  item.albedo_texture_id = *albedo;
  auto normal = r.read_u64();
  if (!normal) {
    return Err(normal.error());
  }
  item.normal_texture_id = *normal;
  auto selected = r.read_bool();
  if (!selected) {
    return Err(selected.error());
  }
  item.selected = *selected;
  auto lines = r.read_bool();
  if (!lines) {
    return Err(lines.error());
  }
  item.lines = *lines;
  item.tex = TextureTransform{};
  if (version >= kTextureTransformFormatVersion) {
    auto sx = r.read_f32();
    if (!sx) {
      return Err(sx.error());
    }
    auto sy = r.read_f32();
    if (!sy) {
      return Err(sy.error());
    }
    auto ox = r.read_f32();
    if (!ox) {
      return Err(ox.error());
    }
    auto oy = r.read_f32();
    if (!oy) {
      return Err(oy.error());
    }
    auto rot = r.read_f32();
    if (!rot) {
      return Err(rot.error());
    }
    auto world = r.read_f32();
    if (!world) {
      return Err(world.error());
    }
    item.tex.scale = {*sx, *sy};
    item.tex.offset = {*ox, *oy};
    item.tex.rotation = *rot;
    item.tex.world_scale = *world;
  }
  item.orm_texture_id = 0;
  if (version >= kOrmTextureFormatVersion) {
    auto orm = r.read_u64();
    if (!orm) {
      return Err(orm.error());
    }
    item.orm_texture_id = *orm;
  }
  return {};
}

Result<void> write_texture(BinaryWriter& w, std::uint64_t id, const TextureAsset& tex) {
  if (auto r = w.write_u64(id); !r) {
    return r;
  }
  if (auto r = w.write_u32(tex.width); !r) {
    return r;
  }
  if (auto r = w.write_u32(tex.height); !r) {
    return r;
  }
  if (auto r = w.write_u64(static_cast<std::uint64_t>(tex.rgba.size())); !r) {
    return r;
  }
  if (!tex.rgba.empty()) {
    if (auto r = w.write_bytes(tex.rgba.data(), tex.rgba.size()); !r) {
      return r;
    }
  }
  return w.write_bool(tex.srgb);
}

Result<void> read_texture(BinaryReader& r, std::uint64_t& id, TextureAsset& tex) {
  auto id_v = r.read_u64();
  if (!id_v) {
    return Err(id_v.error());
  }
  id = *id_v;
  auto width = r.read_u32();
  if (!width) {
    return Err(width.error());
  }
  tex.width = *width;
  auto height = r.read_u32();
  if (!height) {
    return Err(height.error());
  }
  tex.height = *height;
  auto size = r.read_u64();
  if (!size) {
    return Err(size.error());
  }
  if (*size > r.remaining()) {
    return Err("render_scene: texture size too large");
  }
  tex.rgba.resize(static_cast<std::size_t>(*size));
  if (*size > 0) {
    if (auto bytes = r.read_bytes(tex.rgba.data(), tex.rgba.size()); !bytes) {
      return Err(bytes.error());
    }
  }
  auto srgb = r.read_bool();
  if (!srgb) {
    return Err(srgb.error());
  }
  tex.srgb = *srgb;
  tex.id = id;
  return {};
}

Result<void> write_debug_graph(BinaryWriter& w, const RenderSceneDebugGraph& graph) {
  if (auto r = w.write_u64(static_cast<std::uint64_t>(graph.nodes.size())); !r) {
    return r;
  }
  for (const RenderSceneNodeDebug& node : graph.nodes) {
    if (auto r = w.write_u64(node.id); !r) {
      return r;
    }
    if (auto r = w.write_string(node.name); !r) {
      return r;
    }
    if (auto r = w.write_u64(node.parent); !r) {
      return r;
    }
    if (auto r = w.write_u64(static_cast<std::uint64_t>(node.children.size())); !r) {
      return r;
    }
    for (std::uint64_t child : node.children) {
      if (auto r = w.write_u64(child); !r) {
        return r;
      }
    }
    if (auto r = w.write_u64(node.mesh_asset_id); !r) {
      return r;
    }
    if (auto r = write_mat4(w, node.local_transform); !r) {
      return r;
    }
    if (auto r = write_mat4(w, node.world_transform); !r) {
      return r;
    }
    if (auto r = write_aabb(w, node.local_bounds); !r) {
      return r;
    }
    if (auto r = write_aabb(w, node.world_bounds); !r) {
      return r;
    }
    if (auto r = w.write_bool(node.selected); !r) {
      return r;
    }
  }

  if (auto r = w.write_u64(static_cast<std::uint64_t>(graph.lod_sets.size())); !r) {
    return r;
  }
  std::vector<std::uint64_t> geometry_ids;
  geometry_ids.reserve(graph.lod_sets.size());
  for (const auto& [id, _] : graph.lod_sets) {
    geometry_ids.push_back(id);
  }
  std::sort(geometry_ids.begin(), geometry_ids.end());
  for (std::uint64_t id : geometry_ids) {
    const LodMeshSet& set = graph.lod_sets.at(id);
    if (auto r = w.write_u64(id); !r) {
      return r;
    }
    if (auto r = w.write_u64(set.coarse); !r) {
      return r;
    }
    if (auto r = w.write_u64(set.work); !r) {
      return r;
    }
    if (auto r = w.write_u64(set.close); !r) {
      return r;
    }
  }

  if (auto r = w.write_u64(static_cast<std::uint64_t>(graph.lod_by_node.size())); !r) {
    return r;
  }
  for (const auto& [node_id, lod] : graph.lod_by_node) {
    if (auto r = w.write_u64(node_id); !r) {
      return r;
    }
    if (auto r = w.write_u8(static_cast<std::uint8_t>(lod)); !r) {
      return r;
    }
  }
  return {};
}

Result<void> read_debug_graph(BinaryReader& r, RenderSceneDebugGraph& graph) {
  auto node_count = r.read_u64();
  if (!node_count) {
    return Err(node_count.error());
  }
  graph.nodes.resize(static_cast<std::size_t>(*node_count));
  for (RenderSceneNodeDebug& node : graph.nodes) {
    auto id = r.read_u64();
    if (!id) {
      return Err(id.error());
    }
    node.id = *id;
    auto name = r.read_string();
    if (!name) {
      return Err(name.error());
    }
    node.name = std::move(*name);
    auto parent = r.read_u64();
    if (!parent) {
      return Err(parent.error());
    }
    node.parent = *parent;
    auto child_count = r.read_u64();
    if (!child_count) {
      return Err(child_count.error());
    }
    node.children.resize(static_cast<std::size_t>(*child_count));
    for (std::uint64_t& child : node.children) {
      auto child_v = r.read_u64();
      if (!child_v) {
        return Err(child_v.error());
      }
      child = *child_v;
    }
    auto mesh_id = r.read_u64();
    if (!mesh_id) {
      return Err(mesh_id.error());
    }
    node.mesh_asset_id = *mesh_id;
    if (auto res = read_mat4(r, node.local_transform); !res) {
      return res;
    }
    if (auto res = read_mat4(r, node.world_transform); !res) {
      return res;
    }
    if (auto res = read_aabb(r, node.local_bounds); !res) {
      return res;
    }
    if (auto res = read_aabb(r, node.world_bounds); !res) {
      return res;
    }
    auto selected = r.read_bool();
    if (!selected) {
      return Err(selected.error());
    }
    node.selected = *selected;
  }

  auto lod_set_count = r.read_u64();
  if (!lod_set_count) {
    return Err(lod_set_count.error());
  }
  for (std::uint64_t i = 0; i < *lod_set_count; ++i) {
    auto id = r.read_u64();
    if (!id) {
      return Err(id.error());
    }
    LodMeshSet set{};
    auto coarse = r.read_u64();
    if (!coarse) {
      return Err(coarse.error());
    }
    set.coarse = *coarse;
    auto work = r.read_u64();
    if (!work) {
      return Err(work.error());
    }
    set.work = *work;
    auto close = r.read_u64();
    if (!close) {
      return Err(close.error());
    }
    set.close = *close;
    graph.lod_sets.emplace(*id, set);
  }

  auto lod_count = r.read_u64();
  if (!lod_count) {
    return Err(lod_count.error());
  }
  for (std::uint64_t i = 0; i < *lod_count; ++i) {
    auto node_id = r.read_u64();
    if (!node_id) {
      return Err(node_id.error());
    }
    auto lod = r.read_u8();
    if (!lod) {
      return Err(lod.error());
    }
    if (*lod <= static_cast<std::uint8_t>(MeshLod::Close)) {
      graph.lod_by_node[*node_id] = static_cast<MeshLod>(*lod);
    }
  }
  return {};
}

}  // namespace

Result<std::vector<std::uint8_t>> serialize_render_scene(const RenderScene& scene) {
  BinaryWriter meta_w;
  if (auto r = meta_w.write_string(scene.source); !r) {
    return Err(r.error());
  }
  if (auto r = meta_w.write_u64(static_cast<std::uint64_t>(scene.meshes.size())); !r) {
    return Err(r.error());
  }
  if (auto r = meta_w.write_u64(static_cast<std::uint64_t>(scene.items.size())); !r) {
    return Err(r.error());
  }
  if (auto r = meta_w.write_u64(render_scene_triangle_count(scene)); !r) {
    return Err(r.error());
  }
  if (auto r = meta_w.write_string(render_scene_digest(scene)); !r) {
    return Err(r.error());
  }
  if (auto r = meta_w.write_u64(static_cast<std::uint64_t>(scene.textures.size())); !r) {
    return Err(r.error());
  }

  BinaryWriter view_w;
  if (auto r = write_view(view_w, scene.view); !r) {
    return Err(r.error());
  }

  BinaryWriter mesh_w;
  std::vector<std::uint64_t> mesh_ids;
  mesh_ids.reserve(scene.meshes.size());
  for (const auto& [id, _] : scene.meshes) {
    mesh_ids.push_back(id);
  }
  std::sort(mesh_ids.begin(), mesh_ids.end());
  if (auto r = mesh_w.write_u64(static_cast<std::uint64_t>(mesh_ids.size())); !r) {
    return Err(r.error());
  }
  for (std::uint64_t id : mesh_ids) {
    const MeshCpu& mesh = scene.meshes.at(id);
    if (auto r = mesh_w.write_u64(id); !r) {
      return Err(r.error());
    }
    if (auto r = write_mesh_cpu(mesh_w, mesh); !r) {
      return Err(r.error());
    }
    if (auto r = mesh_w.write_bool(mesh.line_list); !r) {
      return Err(r.error());
    }
    if (auto r = mesh_w.write_bool(mesh.has_texcoord); !r) {
      return Err(r.error());
    }
  }

  BinaryWriter draw_w;
  if (auto r = draw_w.write_u64(static_cast<std::uint64_t>(scene.items.size())); !r) {
    return Err(r.error());
  }
  for (const auto& item : scene.items) {
    if (auto r = write_item(draw_w, item); !r) {
      return Err(r.error());
    }
  }

  BinaryWriter tex_w;
  std::vector<std::uint64_t> tex_ids;
  tex_ids.reserve(scene.textures.size());
  for (const auto& [id, _] : scene.textures) {
    tex_ids.push_back(id);
  }
  std::sort(tex_ids.begin(), tex_ids.end());
  if (auto r = tex_w.write_u64(static_cast<std::uint64_t>(tex_ids.size())); !r) {
    return Err(r.error());
  }
  for (std::uint64_t id : tex_ids) {
    if (auto r = write_texture(tex_w, id, scene.textures.at(id)); !r) {
      return Err(r.error());
    }
  }

  const std::uint32_t chunk_count =
      4u + (scene.textures.empty() ? 0u : 1u) + (scene.hidden_node_ids.empty() ? 0u : 1u) +
      (scene.debug_graph.empty() ? 0u : 1u);

  BinaryWriter file;
  if (auto r = file.write_bytes(kMagic, 4); !r) {
    return Err(r.error());
  }
  if (auto r = file.write_u32(kFormatVersion); !r) {
    return Err(r.error());
  }
  if (auto r = file.write_u32(chunk_count); !r) {
    return Err(r.error());
  }
  if (auto r = append_chunk(file, kChunkMeta, meta_w.data()); !r) {
    return Err(r.error());
  }
  if (auto r = append_chunk(file, kChunkView, view_w.data()); !r) {
    return Err(r.error());
  }
  if (auto r = append_chunk(file, kChunkMesh, mesh_w.data()); !r) {
    return Err(r.error());
  }
  if (auto r = append_chunk(file, kChunkDraw, draw_w.data()); !r) {
    return Err(r.error());
  }
  if (!scene.textures.empty()) {
    if (auto r = append_chunk(file, kChunkTex, tex_w.data()); !r) {
      return Err(r.error());
    }
  }
  if (!scene.hidden_node_ids.empty()) {
    BinaryWriter hidn_w;
    std::vector<std::uint64_t> hidden = scene.hidden_node_ids;
    std::sort(hidden.begin(), hidden.end());
    hidden.erase(std::unique(hidden.begin(), hidden.end()), hidden.end());
    if (auto r = hidn_w.write_u64(static_cast<std::uint64_t>(hidden.size())); !r) {
      return Err(r.error());
    }
    for (std::uint64_t id : hidden) {
      if (auto r = hidn_w.write_u64(id); !r) {
        return Err(r.error());
      }
    }
    if (auto r = append_chunk(file, kChunkHidn, hidn_w.data()); !r) {
      return Err(r.error());
    }
  }
  if (!scene.debug_graph.empty()) {
    BinaryWriter graph_w;
    if (auto r = write_debug_graph(graph_w, scene.debug_graph); !r) {
      return Err(r.error());
    }
    if (auto r = append_chunk(file, kChunkSgrf, graph_w.data()); !r) {
      return Err(r.error());
    }
  }
  return file.release();
}

Result<RenderScene> deserialize_render_scene(std::span<const std::uint8_t> bytes) {
  BinaryReader reader(bytes);
  char magic[4]{};
  if (auto r = reader.read_bytes(magic, 4); !r) {
    return Err(r.error());
  }
  if (std::memcmp(magic, kMagic, 4) != 0) {
    return Err("Not a Tamias render scene (bad magic)");
  }
  auto version = reader.read_u32();
  if (!version) {
    return Err(version.error());
  }
  if (*version < kMinFormatVersion || *version > kFormatVersion) {
    return Err("Unsupported render scene version: " + std::to_string(*version));
  }
  auto chunk_count = reader.read_u32();
  if (!chunk_count) {
    return Err(chunk_count.error());
  }

  RenderScene scene;
  scene.version = *version;
  bool has_mesh = false;
  bool has_draw = false;

  for (std::uint32_t i = 0; i < *chunk_count; ++i) {
    auto id = reader.read_u32();
    if (!id) {
      return Err(id.error());
    }
    auto size = reader.read_u64();
    if (!size) {
      return Err(size.error());
    }
    if (*size > reader.remaining()) {
      return Err("render_scene: chunk size exceeds remaining data");
    }
    std::vector<std::uint8_t> chunk(static_cast<std::size_t>(*size));
    if (*size > 0) {
      if (auto r = reader.read_bytes(chunk.data(), chunk.size()); !r) {
        return Err(r.error());
      }
    }
    BinaryReader chunk_r(chunk);

    if (*id == kChunkMeta) {
      auto source = chunk_r.read_string();
      if (!source) {
        return Err(source.error());
      }
      scene.source = std::move(*source);
      // mesh_count / item_count / tris / digest are informational; skip if present.
    } else if (*id == kChunkView) {
      if (auto res = read_view(chunk_r, scene.view); !res) {
        return Err(res.error());
      }
    } else if (*id == kChunkMesh) {
      auto count = chunk_r.read_u64();
      if (!count) {
        return Err(count.error());
      }
      for (std::uint64_t m = 0; m < *count; ++m) {
        auto mesh_id = chunk_r.read_u64();
        if (!mesh_id) {
          return Err(mesh_id.error());
        }
        MeshCpu mesh{};
        if (auto res = read_mesh_cpu(chunk_r, mesh); !res) {
          return Err(res.error());
        }
        auto line_list = chunk_r.read_bool();
        if (!line_list) {
          return Err(line_list.error());
        }
        mesh.line_list = *line_list;
        auto has_uv = chunk_r.read_bool();
        if (!has_uv) {
          return Err(has_uv.error());
        }
        mesh.has_texcoord = *has_uv;
        scene.meshes.emplace(*mesh_id, std::move(mesh));
      }
      has_mesh = true;
    } else if (*id == kChunkDraw) {
      auto count = chunk_r.read_u64();
      if (!count) {
        return Err(count.error());
      }
      scene.items.resize(static_cast<std::size_t>(*count));
      for (auto& item : scene.items) {
        if (auto res = read_item(chunk_r, item, *version); !res) {
          return Err(res.error());
        }
      }
      has_draw = true;
    } else if (*id == kChunkTex) {
      auto count = chunk_r.read_u64();
      if (!count) {
        return Err(count.error());
      }
      for (std::uint64_t t = 0; t < *count; ++t) {
        std::uint64_t tex_id = 0;
        TextureAsset tex{};
        if (auto res = read_texture(chunk_r, tex_id, tex); !res) {
          return Err(res.error());
        }
        scene.textures.emplace(tex_id, std::move(tex));
      }
    } else if (*id == kChunkHidn) {
      auto count = chunk_r.read_u64();
      if (!count) {
        return Err(count.error());
      }
      scene.hidden_node_ids.resize(static_cast<std::size_t>(*count));
      for (auto& hid : scene.hidden_node_ids) {
        auto node = chunk_r.read_u64();
        if (!node) {
          return Err(node.error());
        }
        hid = *node;
      }
    } else if (*id == kChunkSgrf) {
      if (auto res = read_debug_graph(chunk_r, scene.debug_graph); !res) {
        return Err(res.error());
      }
    }
  }

  if (!has_mesh || !has_draw) {
    return Err("render_scene: missing MESH or DRAW chunk");
  }
  return scene;
}

Result<void> save_render_scene(const std::filesystem::path& path, const RenderScene& scene) {
  auto bytes = serialize_render_scene(scene);
  if (!bytes) {
    return Err(bytes.error());
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return Err("Failed to open file for writing: " + path_to_utf8(path));
  }
  out.write(reinterpret_cast<const char*>(bytes->data()),
            static_cast<std::streamsize>(bytes->size()));
  if (!out) {
    return Err("Failed to write render scene: " + path_to_utf8(path));
  }
  return {};
}

Result<RenderScene> load_render_scene(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Err("Failed to open render scene: " + path_to_utf8(path));
  }
  in.seekg(0, std::ios::end);
  const auto size = in.tellg();
  if (size < 0) {
    return Err("Failed to read render scene: " + path_to_utf8(path));
  }
  in.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (size > 0) {
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in) {
      return Err("Failed to read render scene: " + path_to_utf8(path));
    }
  }
  return deserialize_render_scene(bytes);
}

bool is_render_scene_path(const std::filesystem::path& path) {
  return path_extension_lower(path) == ".trscn";
}

}  // namespace tamias
