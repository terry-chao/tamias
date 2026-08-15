#include "document_io.h"

#include "graphics/mesh.h"
#include "io/binary_archive.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>

namespace tamias {
namespace {

constexpr char kMagic[4] = {'T', 'M', 'A', 'S'};
constexpr std::uint32_t kFormatVersion = 2;

constexpr std::uint32_t fourcc(char a, char b, char c, char d) {
  return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24);
}

constexpr std::uint32_t kChunkMeta = fourcc('M', 'E', 'T', 'A');
constexpr std::uint32_t kChunkMesh = fourcc('M', 'E', 'S', 'H');
constexpr std::uint32_t kChunkScen = fourcc('S', 'C', 'E', 'N');
constexpr std::uint32_t kChunkView = fourcc('V', 'I', 'E', 'W');

Result<void> write_vec2(BinaryWriter& w, const Vec2& v) {
  if (auto r = w.write_f32(v.x); !r) {
    return r;
  }
  return w.write_f32(v.y);
}

Result<void> read_vec2(BinaryReader& r, Vec2& v) {
  auto x = r.read_f32();
  if (!x) {
    return Err(x.error());
  }
  auto y = r.read_f32();
  if (!y) {
    return Err(y.error());
  }
  v = {*x, *y};
  return {};
}

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

Result<void> write_vertex(BinaryWriter& w, const Vertex& v) {
  if (auto r = write_vec3(w, v.position); !r) {
    return r;
  }
  if (auto r = write_vec3(w, v.normal); !r) {
    return r;
  }
  if (auto r = write_vec2(w, v.uv); !r) {
    return r;
  }
  return write_vec3(w, v.color);
}

Result<void> read_vertex(BinaryReader& r, Vertex& v) {
  if (auto res = read_vec3(r, v.position); !res) {
    return res;
  }
  if (auto res = read_vec3(r, v.normal); !res) {
    return res;
  }
  if (auto res = read_vec2(r, v.uv); !res) {
    return res;
  }
  return read_vec3(r, v.color);
}

Result<void> write_mesh_cpu(BinaryWriter& w, const MeshCpu& mesh) {
  if (auto r = w.write_u64(static_cast<std::uint64_t>(mesh.vertices.size())); !r) {
    return r;
  }
  for (const auto& v : mesh.vertices) {
    if (auto r = write_vertex(w, v); !r) {
      return r;
    }
  }
  if (auto r = w.write_u64(static_cast<std::uint64_t>(mesh.indices.size())); !r) {
    return r;
  }
  for (std::uint32_t idx : mesh.indices) {
    if (auto r = w.write_u32(idx); !r) {
      return r;
    }
  }
  return write_aabb(w, mesh.bounds);
}

Result<void> read_mesh_cpu(BinaryReader& r, MeshCpu& mesh) {
  auto vert_count = r.read_u64();
  if (!vert_count) {
    return Err(vert_count.error());
  }
  if (*vert_count > r.remaining()) {
    return Err("document_io: vertex count too large");
  }
  mesh.vertices.resize(static_cast<std::size_t>(*vert_count));
  for (auto& v : mesh.vertices) {
    if (auto res = read_vertex(r, v); !res) {
      return res;
    }
  }
  auto index_count = r.read_u64();
  if (!index_count) {
    return Err(index_count.error());
  }
  if (*index_count > r.remaining()) {
    return Err("document_io: index count too large");
  }
  mesh.indices.resize(static_cast<std::size_t>(*index_count));
  for (auto& idx : mesh.indices) {
    auto v = r.read_u32();
    if (!v) {
      return Err(v.error());
    }
    idx = *v;
  }
  if (auto res = read_aabb(r, mesh.bounds); !res) {
    return res;
  }
  if (!mesh.bounds.valid()) {
    recompute_bounds(mesh);
  }
  return {};
}

Result<void> write_mesh_asset(BinaryWriter& w, const MeshAsset& asset) {
  if (auto r = w.write_u64(asset.id); !r) {
    return r;
  }
  if (auto r = w.write_string(asset.name); !r) {
    return r;
  }
  return write_mesh_cpu(w, asset.cpu);
}

Result<void> read_mesh_asset(BinaryReader& r, MeshAsset& asset) {
  auto id = r.read_u64();
  if (!id) {
    return Err(id.error());
  }
  asset.id = *id;
  auto name = r.read_string();
  if (!name) {
    return Err(name.error());
  }
  asset.name = std::move(*name);
  return read_mesh_cpu(r, asset.cpu);
}

Result<void> write_scene_node(BinaryWriter& w, const SceneNode& node) {
  if (auto r = w.write_u64(node.id); !r) {
    return r;
  }
  if (auto r = w.write_string(node.name); !r) {
    return r;
  }
  if (auto r = w.write_u64(node.parent); !r) {
    return r;
  }
  if (auto r = w.write_u64(node.mesh_asset_id); !r) {
    return r;
  }
  if (auto r = write_mat4(w, node.local_transform); !r) {
    return r;
  }
  return write_vec3(w, node.color);
}

Result<void> read_scene_node(BinaryReader& r, SceneNode& node) {
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
  auto mesh_id = r.read_u64();
  if (!mesh_id) {
    return Err(mesh_id.error());
  }
  node.mesh_asset_id = *mesh_id;
  node.selected = false;
  if (auto res = read_mat4(r, node.local_transform); !res) {
    return res;
  }
  return read_vec3(r, node.color);
}

Result<void> write_viewport(BinaryWriter& w, const ViewportState& vp) {
  if (auto r = write_vec3(w, vp.target); !r) {
    return r;
  }
  if (auto r = w.write_f32(vp.distance); !r) {
    return r;
  }
  if (auto r = w.write_f32(vp.yaw); !r) {
    return r;
  }
  if (auto r = w.write_f32(vp.pitch); !r) {
    return r;
  }
  if (auto r = w.write_f32(vp.fovy); !r) {
    return r;
  }
  if (auto r = w.write_f32(vp.znear); !r) {
    return r;
  }
  if (auto r = w.write_f32(vp.zfar); !r) {
    return r;
  }
  return w.write_u32(static_cast<std::uint32_t>(vp.render_mode));
}

Result<void> read_viewport(BinaryReader& r, ViewportState& vp) {
  if (auto res = read_vec3(r, vp.target); !res) {
    return res;
  }
  auto distance = r.read_f32();
  if (!distance) {
    return Err(distance.error());
  }
  vp.distance = *distance;
  auto yaw = r.read_f32();
  if (!yaw) {
    return Err(yaw.error());
  }
  vp.yaw = *yaw;
  auto pitch = r.read_f32();
  if (!pitch) {
    return Err(pitch.error());
  }
  vp.pitch = *pitch;
  auto fovy = r.read_f32();
  if (!fovy) {
    return Err(fovy.error());
  }
  vp.fovy = *fovy;
  auto znear = r.read_f32();
  if (!znear) {
    return Err(znear.error());
  }
  vp.znear = *znear;
  auto zfar = r.read_f32();
  if (!zfar) {
    return Err(zfar.error());
  }
  vp.zfar = *zfar;
  auto mode = r.read_u32();
  if (!mode) {
    return Err(mode.error());
  }
  vp.render_mode = static_cast<ViewRenderMode>(*mode);
  return {};
}

Result<void> write_document_body(BinaryWriter& w, const Document& document) {
  if (auto r = w.write_string(document.name()); !r) {
    return r;
  }
  if (auto r = w.write_u64(document.next_mesh_id()); !r) {
    return r;
  }
  if (auto r = w.write_u64(document.scene().next_id()); !r) {
    return r;
  }

  if (auto r = w.write_u64(static_cast<std::uint64_t>(document.meshes().size())); !r) {
    return r;
  }
  // Stable order by id for deterministic snapshots.
  std::vector<std::uint64_t> mesh_ids;
  mesh_ids.reserve(document.meshes().size());
  for (const auto& [id, _] : document.meshes()) {
    mesh_ids.push_back(id);
  }
  std::sort(mesh_ids.begin(), mesh_ids.end());
  for (std::uint64_t id : mesh_ids) {
    if (auto r = write_mesh_asset(w, document.meshes().at(id)); !r) {
      return r;
    }
  }

  if (auto r = w.write_u64(static_cast<std::uint64_t>(document.scene().nodes().size())); !r) {
    return r;
  }
  for (const auto& node : document.scene().nodes()) {
    if (auto r = write_scene_node(w, node); !r) {
      return r;
    }
  }
  return {};
}

Result<Document> read_document_body(BinaryReader& r) {
  auto name = r.read_string();
  if (!name) {
    return Err(name.error());
  }
  Document document(std::move(*name));

  auto next_mesh = r.read_u64();
  if (!next_mesh) {
    return Err(next_mesh.error());
  }
  auto next_node = r.read_u64();
  if (!next_node) {
    return Err(next_node.error());
  }

  auto mesh_count = r.read_u64();
  if (!mesh_count) {
    return Err(mesh_count.error());
  }
  for (std::uint64_t i = 0; i < *mesh_count; ++i) {
    MeshAsset asset{};
    if (auto res = read_mesh_asset(r, asset); !res) {
      return Err(res.error());
    }
    document.insert_mesh(std::move(asset));
  }

  auto node_count = r.read_u64();
  if (!node_count) {
    return Err(node_count.error());
  }
  for (std::uint64_t i = 0; i < *node_count; ++i) {
    SceneNode node{};
    if (auto res = read_scene_node(r, node); !res) {
      return Err(res.error());
    }
    document.scene().insert_node(std::move(node));
  }

  document.set_next_mesh_id(*next_mesh);
  document.scene().set_next_id(*next_node);
  document.recompute_scene();
  document.clear_dirty();
  return document;
}

Result<void> append_chunk(BinaryWriter& file, std::uint32_t id, const std::vector<std::uint8_t>& payload) {
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

}  // namespace

Result<std::vector<std::uint8_t>> serialize_document(const Document& document) {
  BinaryWriter w;
  if (auto r = write_document_body(w, document); !r) {
    return Err(r.error());
  }
  return w.release();
}

Result<Document> deserialize_document(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes);
  return read_document_body(r);
}

bool is_tdoc_document_path(const std::filesystem::path& path) {
  auto ext = path.extension().string();
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext == ".tdoc";
}

Result<void> save_document(const std::filesystem::path& path, const Document& document,
                           const ViewportState& viewport) {
  BinaryWriter meta_w;
  if (auto r = meta_w.write_string(document.name()); !r) {
    return r;
  }
  if (auto r = meta_w.write_u64(document.next_mesh_id()); !r) {
    return r;
  }
  if (auto r = meta_w.write_u64(document.scene().next_id()); !r) {
    return r;
  }

  BinaryWriter mesh_w;
  std::vector<std::uint64_t> mesh_ids;
  mesh_ids.reserve(document.meshes().size());
  for (const auto& [id, _] : document.meshes()) {
    mesh_ids.push_back(id);
  }
  std::sort(mesh_ids.begin(), mesh_ids.end());
  if (auto r = mesh_w.write_u64(static_cast<std::uint64_t>(mesh_ids.size())); !r) {
    return r;
  }
  for (std::uint64_t id : mesh_ids) {
    if (auto r = write_mesh_asset(mesh_w, document.meshes().at(id)); !r) {
      return r;
    }
  }

  BinaryWriter scen_w;
  if (auto r = scen_w.write_u64(static_cast<std::uint64_t>(document.scene().nodes().size())); !r) {
    return r;
  }
  for (const auto& node : document.scene().nodes()) {
    if (auto r = write_scene_node(scen_w, node); !r) {
      return r;
    }
  }

  BinaryWriter view_w;
  if (auto r = write_viewport(view_w, viewport); !r) {
    return r;
  }

  BinaryWriter file;
  if (auto r = file.write_bytes(kMagic, 4); !r) {
    return r;
  }
  if (auto r = file.write_u32(kFormatVersion); !r) {
    return r;
  }
  if (auto r = file.write_u32(4); !r) {  // chunk_count
    return r;
  }
  if (auto r = append_chunk(file, kChunkMeta, meta_w.data()); !r) {
    return r;
  }
  if (auto r = append_chunk(file, kChunkMesh, mesh_w.data()); !r) {
    return r;
  }
  if (auto r = append_chunk(file, kChunkScen, scen_w.data()); !r) {
    return r;
  }
  if (auto r = append_chunk(file, kChunkView, view_w.data()); !r) {
    return r;
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return Err("Failed to open file for writing: " + path.string());
  }
  const auto& bytes = file.data();
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  if (!out) {
    return Err("Failed to write document: " + path.string());
  }
  return {};
}

Result<LoadedDocument> load_document(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Err("Failed to open file: " + path.string());
  }
  in.seekg(0, std::ios::end);
  const auto file_size = static_cast<std::size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(file_size);
  if (file_size > 0) {
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(file_size));
    if (!in) {
      return Err("Failed to read file: " + path.string());
    }
  }

  BinaryReader reader(bytes);
  char magic[4]{};
  if (auto r = reader.read_bytes(magic, 4); !r) {
    return Err(r.error());
  }
  if (std::memcmp(magic, kMagic, 4) != 0) {
    return Err("Not a Tamias document (bad magic)");
  }
  auto version = reader.read_u32();
  if (!version) {
    return Err(version.error());
  }
  if (*version != kFormatVersion) {
    return Err("Unsupported Tamias document version: " + std::to_string(*version));
  }
  auto chunk_count = reader.read_u32();
  if (!chunk_count) {
    return Err(chunk_count.error());
  }

  std::string name = "Untitled";
  std::uint64_t next_mesh_id = 1;
  std::uint64_t next_node_id = 1;
  std::vector<MeshAsset> meshes;
  std::vector<SceneNode> nodes;
  ViewportState viewport{};
  bool has_meta = false;
  bool has_mesh = false;
  bool has_scen = false;
  bool has_view = false;

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
      return Err("document_io: chunk size exceeds remaining data");
    }
    std::vector<std::uint8_t> chunk(static_cast<std::size_t>(*size));
    if (*size > 0) {
      if (auto r = reader.read_bytes(chunk.data(), chunk.size()); !r) {
        return Err(r.error());
      }
    }
    BinaryReader chunk_r(chunk);

    if (*id == kChunkMeta) {
      auto n = chunk_r.read_string();
      if (!n) {
        return Err(n.error());
      }
      name = std::move(*n);
      auto nm = chunk_r.read_u64();
      if (!nm) {
        return Err(nm.error());
      }
      next_mesh_id = *nm;
      auto nn = chunk_r.read_u64();
      if (!nn) {
        return Err(nn.error());
      }
      next_node_id = *nn;
      has_meta = true;
    } else if (*id == kChunkMesh) {
      auto count = chunk_r.read_u64();
      if (!count) {
        return Err(count.error());
      }
      meshes.clear();
      meshes.reserve(static_cast<std::size_t>(*count));
      for (std::uint64_t m = 0; m < *count; ++m) {
        MeshAsset asset{};
        if (auto res = read_mesh_asset(chunk_r, asset); !res) {
          return Err(res.error());
        }
        meshes.push_back(std::move(asset));
      }
      has_mesh = true;
    } else if (*id == kChunkScen) {
      auto count = chunk_r.read_u64();
      if (!count) {
        return Err(count.error());
      }
      nodes.clear();
      nodes.reserve(static_cast<std::size_t>(*count));
      for (std::uint64_t n = 0; n < *count; ++n) {
        SceneNode node{};
        if (auto res = read_scene_node(chunk_r, node); !res) {
          return Err(res.error());
        }
        nodes.push_back(std::move(node));
      }
      has_scen = true;
    } else if (*id == kChunkView) {
      if (auto res = read_viewport(chunk_r, viewport); !res) {
        return Err(res.error());
      }
      has_view = true;
    } else {
      // Unknown chunk: already consumed via read_bytes into chunk; skip.
    }
  }

  if (!has_meta || !has_mesh || !has_scen) {
    return Err("Incomplete Tamias document (missing META/MESH/SCEN)");
  }

  LoadedDocument loaded;
  loaded.document = Document(std::move(name));
  loaded.document.set_path(path);
  for (auto& asset : meshes) {
    loaded.document.insert_mesh(std::move(asset));
  }
  for (auto& node : nodes) {
    loaded.document.scene().insert_node(std::move(node));
  }
  loaded.document.set_next_mesh_id(next_mesh_id);
  loaded.document.scene().set_next_id(next_node_id);
  loaded.document.recompute_scene();
  loaded.document.clear_dirty();
  loaded.viewport = viewport;
  loaded.has_viewport = has_view;
  return loaded;
}

}  // namespace tamias
