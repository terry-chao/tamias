#include "engine/render/render_scene.h"

#include "engine/core/fs_utf8.h"
#include "engine/render/debug_vertex_overlay.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace tamias {
namespace {

Result<void> write_text_file(const std::filesystem::path& path, std::string_view text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return Err("Failed to write " + path_to_utf8(path));
  }
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!out) {
    return Err("Failed to write " + path_to_utf8(path));
  }
  return {};
}

Result<void> write_debug_obj(const std::filesystem::path& path, const MeshCpu& mesh) {
  if (mesh.vertices.empty()) {
    return Err("mesh has no vertices: " + path_to_utf8(path));
  }
  if (!mesh.line_list && (mesh.indices.empty() || mesh.indices.size() % 3 != 0)) {
    return Err("mesh index count is not a multiple of 3: " + path_to_utf8(path));
  }
  std::ofstream out(path);
  if (!out) {
    return Err("failed to open " + path_to_utf8(path));
  }
  out << "# Tamias render-scene debug\n";
  out << std::setprecision(9) << std::fixed;
  const bool write_colors = mesh_has_vertex_colors(mesh);
  for (const Vertex& v : mesh.vertices) {
    out << "v " << v.position.x << ' ' << v.position.y << ' ' << v.position.z;
    if (write_colors) {
      out << ' ' << v.color.x << ' ' << v.color.y << ' ' << v.color.z;
    }
    out << '\n';
  }
  for (const Vertex& v : mesh.vertices) {
    out << "vn " << v.normal.x << ' ' << v.normal.y << ' ' << v.normal.z << '\n';
  }
  for (const Vertex& v : mesh.vertices) {
    out << "vt " << v.uv.x << ' ' << v.uv.y << '\n';
  }
  if (mesh.line_list) {
    for (std::size_t i = 0; i + 1 < mesh.indices.size(); i += 2) {
      out << "l " << (mesh.indices[i] + 1) << ' ' << (mesh.indices[i + 1] + 1) << '\n';
    }
  } else {
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
      const auto i0 = mesh.indices[i] + 1;
      const auto i1 = mesh.indices[i + 1] + 1;
      const auto i2 = mesh.indices[i + 2] + 1;
      out << "f " << i0 << '/' << i0 << '/' << i0 << ' ' << i1 << '/' << i1 << '/' << i1 << ' '
          << i2 << '/' << i2 << '/' << i2 << '\n';
    }
  }
  if (!out) {
    return Err("failed while writing " + path_to_utf8(path));
  }
  return {};
}

MeshCpu transformed_mesh(const MeshCpu& mesh, const Mat4& transform) {
  MeshCpu out = mesh;
  for (Vertex& v : out.vertices) {
    v.position = transform * v.position;
    v.normal = transform_normal_affine(transform, v.normal);
  }
  recompute_bounds(out);
  return out;
}

Result<void> write_ppm(const std::filesystem::path& path, const TextureAsset& tex) {
  const std::size_t expected =
      static_cast<std::size_t>(tex.width) * static_cast<std::size_t>(tex.height) * 4u;
  if (tex.width == 0 || tex.height == 0 || tex.rgba.size() < expected) {
    return Err("texture pixels incomplete: " + path_to_utf8(path));
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return Err("failed to open " + path_to_utf8(path));
  }
  out << "P6\n" << tex.width << ' ' << tex.height << "\n255\n";
  for (std::size_t i = 0; i < expected; i += 4) {
    out.put(static_cast<char>(tex.rgba[i]));
    out.put(static_cast<char>(tex.rgba[i + 1]));
    out.put(static_cast<char>(tex.rgba[i + 2]));
  }
  if (!out) {
    return Err("failed while writing " + path_to_utf8(path));
  }
  return {};
}

}  // namespace

Result<void> write_render_scene_debug_files(const std::filesystem::path& inspect_path,
                                            const std::filesystem::path& debug_dir,
                                            const RenderScene& scene) {
  if (auto r = write_text_file(inspect_path, inspect_render_scene(scene)); !r) {
    return r;
  }

  std::error_code ec;
  std::filesystem::remove_all(debug_dir, ec);
  std::filesystem::create_directories(debug_dir, ec);
  if (ec) {
    return Err("failed to create " + path_to_utf8(debug_dir) + ": " + ec.message());
  }

  std::ostringstream readme;
  readme << "Tamias render-scene debug dump\n";
  readme << "source: " << (scene.source.empty() ? "(unnamed)" : scene.source) << '\n';
  readme << "digest: " << render_scene_digest(scene) << '\n';
  readme << "mesh_<id>.obj        asset-space mesh (open in any DCC; v/vt/vn)\n";
  readme << "draw_*_node_*.obj    world-space copy of that draw (transform applied)\n";
  readme << "tex_<id>.ppm         albedo/normal pixels (any image viewer)\n";
  readme << "Parent inspect.txt has every vertex, matrix, and draw field.\n";
  if (auto r = write_text_file(debug_dir / "README.txt", readme.str()); !r) {
    return r;
  }

  std::vector<std::uint64_t> mesh_ids;
  mesh_ids.reserve(scene.meshes.size());
  for (const auto& [id, _] : scene.meshes) {
    mesh_ids.push_back(id);
  }
  std::sort(mesh_ids.begin(), mesh_ids.end());
  for (std::uint64_t id : mesh_ids) {
    const auto path = debug_dir / ("mesh_" + std::to_string(id) + ".obj");
    if (auto r = write_debug_obj(path, scene.meshes.at(id)); !r) {
      return r;
    }
  }

  for (std::size_t i = 0; i < scene.items.size(); ++i) {
    const SceneDrawItem& item = scene.items[i];
    const auto it = scene.meshes.find(item.mesh_asset_id);
    if (it == scene.meshes.end()) {
      continue;
    }
    MeshCpu world = transformed_mesh(it->second, item.transform);
    const auto path = debug_dir / ("draw_" + std::to_string(i) + "_node_" +
                                   std::to_string(item.node_id) + ".obj");
    if (auto r = write_debug_obj(path, world); !r) {
      return r;
    }
  }

  std::vector<std::uint64_t> tex_ids;
  tex_ids.reserve(scene.textures.size());
  for (const auto& [id, _] : scene.textures) {
    tex_ids.push_back(id);
  }
  std::sort(tex_ids.begin(), tex_ids.end());
  for (std::uint64_t id : tex_ids) {
    const auto path = debug_dir / ("tex_" + std::to_string(id) + ".ppm");
    if (auto r = write_ppm(path, scene.textures.at(id)); !r) {
      return r;
    }
  }
  return {};
}

Result<void> write_render_scene_debug_sidecars(const std::filesystem::path& trscn_path,
                                               const RenderScene& scene) {
  std::filesystem::path inspect = trscn_path;
  inspect.replace_extension(".inspect.txt");
  std::filesystem::path debug = trscn_path;
  debug.replace_extension(".debug");
  return write_render_scene_debug_files(inspect, debug, scene);
}

Result<void> write_render_scene_debug_draw(const std::filesystem::path& obj_path,
                                           const RenderScene& scene, std::size_t item_index) {
  if (item_index >= scene.items.size()) {
    return Err("draw index out of range");
  }
  const SceneDrawItem& item = scene.items[item_index];
  const auto it = scene.meshes.find(item.mesh_asset_id);
  if (it == scene.meshes.end()) {
    return Err("mesh " + std::to_string(item.mesh_asset_id) + " is not in this scene");
  }
  return write_debug_obj(obj_path, transformed_mesh(it->second, item.transform));
}

}  // namespace tamias
