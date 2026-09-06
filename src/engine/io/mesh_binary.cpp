#include "mesh_binary.h"

#include "engine/math/math.h"

namespace tamias {
namespace {

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

}  // namespace

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
    return Err("mesh_binary: vertex count too large");
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
    return Err("mesh_binary: index count too large");
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

}  // namespace tamias
