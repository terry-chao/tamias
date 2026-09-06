#include "engine/render/render_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

namespace tamias {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void fnv_u8(std::uint64_t& h, std::uint8_t v) {
  h ^= v;
  h *= kFnvPrime;
}

void fnv_bytes(std::uint64_t& h, const void* data, std::size_t n) {
  const auto* p = static_cast<const std::uint8_t*>(data);
  for (std::size_t i = 0; i < n; ++i) {
    fnv_u8(h, p[i]);
  }
}

void fnv_u32(std::uint64_t& h, std::uint32_t v) { fnv_bytes(h, &v, sizeof(v)); }

void fnv_u64(std::uint64_t& h, std::uint64_t v) { fnv_bytes(h, &v, sizeof(v)); }

void fnv_bool(std::uint64_t& h, bool v) { fnv_u8(h, v ? 1u : 0u); }

void fnv_f32(std::uint64_t& h, float v) {
  const std::int32_t q = static_cast<std::int32_t>(std::lround(static_cast<double>(v) * 1.0e5));
  fnv_bytes(h, &q, sizeof(q));
}

void fnv_vec3(std::uint64_t& h, const Vec3& v) {
  fnv_f32(h, v.x);
  fnv_f32(h, v.y);
  fnv_f32(h, v.z);
}

void fnv_mat4(std::uint64_t& h, const Mat4& m) {
  for (float f : m.m) {
    fnv_f32(h, f);
  }
}

void fnv_aabb(std::uint64_t& h, const Aabb& box) {
  fnv_vec3(h, box.min);
  fnv_vec3(h, box.max);
}

void fnv_item(std::uint64_t& h, const SceneDrawItem& item) {
  fnv_u64(h, item.node_id);
  fnv_u64(h, item.mesh_asset_id);
  fnv_mat4(h, item.transform);
  fnv_aabb(h, item.bounds);
  fnv_vec3(h, item.color);
  fnv_vec3(h, item.category_color);
  fnv_f32(h, item.roughness);
  fnv_f32(h, item.metallic);
  fnv_f32(h, item.opacity);
  fnv_u64(h, item.albedo_texture_id);
  fnv_u64(h, item.normal_texture_id);
  fnv_bool(h, item.selected);
  fnv_bool(h, item.lines);
}

void fnv_mesh(std::uint64_t& h, std::uint64_t id, const MeshCpu& mesh) {
  fnv_u64(h, id);
  fnv_u64(h, static_cast<std::uint64_t>(mesh.vertices.size()));
  fnv_u64(h, static_cast<std::uint64_t>(mesh.indices.size()));
  fnv_bool(h, mesh.line_list);
  fnv_bool(h, mesh.has_texcoord);
  fnv_aabb(h, mesh.bounds);
  for (const auto& v : mesh.vertices) {
    fnv_vec3(h, v.position);
    fnv_vec3(h, v.normal);
    fnv_f32(h, v.uv.x);
    fnv_f32(h, v.uv.y);
    fnv_vec3(h, v.color);
  }
  for (std::uint32_t idx : mesh.indices) {
    fnv_u32(h, idx);
  }
}

void fnv_texture(std::uint64_t& h, std::uint64_t id, const TextureAsset& tex) {
  fnv_u64(h, id);
  fnv_u32(h, tex.width);
  fnv_u32(h, tex.height);
  fnv_bool(h, tex.srgb);
  fnv_u64(h, static_cast<std::uint64_t>(tex.rgba.size()));
  if (!tex.rgba.empty()) {
    fnv_bytes(h, tex.rgba.data(), tex.rgba.size());
  }
}

void take_texture(RenderScene& out, std::uint64_t id,
                  const std::unordered_map<std::uint64_t, TextureAsset>& textures) {
  if (id == 0 || out.textures.contains(id)) {
    return;
  }
  const auto it = textures.find(id);
  if (it != textures.end()) {
    out.textures.emplace(id, it->second);
  }
}

std::string hex_u64(std::uint64_t v) {
  std::ostringstream out;
  out << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << v;
  return out.str();
}

}  // namespace

RenderScene bake_render_scene(std::vector<SceneDrawItem> items,
                              const std::unordered_map<std::uint64_t, MeshCpu>& meshes,
                              RenderScene::View view, std::string source,
                              const std::unordered_map<std::uint64_t, TextureAsset>& textures) {
  std::sort(items.begin(), items.end(), [](const SceneDrawItem& a, const SceneDrawItem& b) {
    if (a.node_id != b.node_id) {
      return a.node_id < b.node_id;
    }
    return a.mesh_asset_id < b.mesh_asset_id;
  });

  RenderScene out;
  out.source = std::move(source);
  out.view = std::move(view);
  out.items.reserve(items.size());
  for (auto& item : items) {
    if (item.mesh_asset_id == 0) {
      continue;
    }
    const auto it = meshes.find(item.mesh_asset_id);
    if (it == meshes.end()) {
      continue;
    }
    out.meshes.emplace(item.mesh_asset_id, it->second);
    take_texture(out, item.albedo_texture_id, textures);
    take_texture(out, item.normal_texture_id, textures);
    out.items.push_back(std::move(item));
  }
  return out;
}

std::uint64_t render_scene_triangle_count(const RenderScene& scene) {
  std::uint64_t n = 0;
  for (const auto& [id, mesh] : scene.meshes) {
    (void)id;
    if (mesh.line_list) {
      continue;
    }
    n += static_cast<std::uint64_t>(mesh.indices.size() / 3);
  }
  return n;
}

std::string render_scene_digest(const RenderScene& scene) {
  std::uint64_t h = kFnvOffset;
  fnv_u32(h, scene.version);
  fnv_u64(h, static_cast<std::uint64_t>(scene.meshes.size()));
  fnv_u64(h, static_cast<std::uint64_t>(scene.textures.size()));
  fnv_u64(h, static_cast<std::uint64_t>(scene.items.size()));
  std::vector<std::uint64_t> mesh_ids;
  mesh_ids.reserve(scene.meshes.size());
  for (const auto& [id, _] : scene.meshes) {
    mesh_ids.push_back(id);
  }
  std::sort(mesh_ids.begin(), mesh_ids.end());
  for (std::uint64_t id : mesh_ids) {
    fnv_mesh(h, id, scene.meshes.at(id));
  }
  std::vector<std::uint64_t> tex_ids;
  tex_ids.reserve(scene.textures.size());
  for (const auto& [id, _] : scene.textures) {
    tex_ids.push_back(id);
  }
  std::sort(tex_ids.begin(), tex_ids.end());
  for (std::uint64_t id : tex_ids) {
    fnv_texture(h, id, scene.textures.at(id));
  }
  for (const auto& item : scene.items) {
    fnv_item(h, item);
  }
  return hex_u64(h);
}

std::string inspect_render_scene(const RenderScene& scene) {
  std::ostringstream out;
  out << "source: " << (scene.source.empty() ? "(unnamed)" : scene.source) << '\n';
  out << "meshes=" << scene.meshes.size() << "  textures=" << scene.textures.size()
      << "  items=" << scene.items.size() << "  tris=" << render_scene_triangle_count(scene)
      << "  digest=" << render_scene_digest(scene) << '\n';
  out << "view mode=" << static_cast<int>(scene.view.mode) << "  size=" << scene.view.width << 'x'
      << scene.view.height << "  distance=" << scene.view.view_distance << '\n';
  for (const auto& item : scene.items) {
    std::uint64_t tris = 0;
    const auto it = scene.meshes.find(item.mesh_asset_id);
    if (it != scene.meshes.end() && !it->second.line_list) {
      tris = static_cast<std::uint64_t>(it->second.indices.size() / 3);
    }
    out << "item node=" << item.node_id << " mesh=" << item.mesh_asset_id << " tris=" << tris;
    if (item.bounds.valid()) {
      out << " aabb=(" << item.bounds.min.x << ',' << item.bounds.min.y << ',' << item.bounds.min.z
          << ")-(" << item.bounds.max.x << ',' << item.bounds.max.y << ',' << item.bounds.max.z
          << ')';
    }
    out << " color=(" << item.color.x << ',' << item.color.y << ',' << item.color.z << ')';
    if (item.selected) {
      out << " selected";
    }
    if (item.lines) {
      out << " lines";
    }
    if (item.albedo_texture_id != 0) {
      out << " albedo=" << item.albedo_texture_id;
    }
    if (item.normal_texture_id != 0) {
      out << " normal=" << item.normal_texture_id;
    }
    out << '\n';
  }
  return out.str();
}

}  // namespace tamias
