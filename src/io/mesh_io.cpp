#include "mesh_io.h"

#include "core/log.h"

#include <rapidobj/rapidobj.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <vector>

namespace tamias {
namespace {

void append_triangle(MeshCpu& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 n) {
  const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back(Vertex{a, n, {}});
  mesh.vertices.push_back(Vertex{b, n, {}});
  mesh.vertices.push_back(Vertex{c, n, {}});
  mesh.indices.push_back(base);
  mesh.indices.push_back(base + 1);
  mesh.indices.push_back(base + 2);
}

template <typename T>
bool read_pod(std::istream& in, T& value) {
  in.read(reinterpret_cast<char*>(&value), sizeof(T));
  return static_cast<bool>(in);
}

// Minimal GLB loader: triangulated POSITION (+ optional NORMAL) meshes only.
Result<MeshCpu> load_glb(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Err("failed to open GLB");
  }
  std::uint32_t magic = 0, version = 0, length = 0;
  if (!read_pod(in, magic) || magic != 0x46546C67u) {  // glTF
    return Err("not a GLB file");
  }
  read_pod(in, version);
  read_pod(in, length);
  (void)version;

  std::uint32_t chunk_len = 0, chunk_type = 0;
  if (!read_pod(in, chunk_len) || !read_pod(in, chunk_type)) {
    return Err("GLB header truncated");
  }
  if (chunk_type != 0x4E4F534Au) {  // JSON
    return Err("GLB missing JSON chunk");
  }
  std::string json(chunk_len, '\0');
  in.read(json.data(), chunk_len);
  if (!in) {
    return Err("GLB JSON read failed");
  }

  // Align to 4 bytes already satisfied by chunk_len padding in valid GLB.
  std::uint32_t bin_len = 0, bin_type = 0;
  std::vector<char> bin;
  if (read_pod(in, bin_len) && read_pod(in, bin_type) && bin_type == 0x004E4942u) {  // BIN
    bin.resize(bin_len);
    in.read(bin.data(), bin_len);
  }

  auto find_number_after = [&](std::string_view key, std::size_t from) -> std::optional<double> {
    const auto pos = json.find(key, from);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    const auto colon = json.find(':', pos + key.size());
    if (colon == std::string::npos) {
      return std::nullopt;
    }
    std::size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\n' || json[i] == '\r' || json[i] == '\t')) {
      ++i;
    }
    char* end = nullptr;
    const double value = std::strtod(json.c_str() + i, &end);
    if (end == json.c_str() + i) {
      return std::nullopt;
    }
    return value;
  };

  // Extremely small parser: locate first mesh primitive attributes + indices.
  const auto meshes_pos = json.find("\"meshes\"");
  if (meshes_pos == std::string::npos) {
    return Err("GLB JSON has no meshes");
  }
  const auto pos_attr = json.find("\"POSITION\"", meshes_pos);
  if (pos_attr == std::string::npos) {
    return Err("GLB mesh missing POSITION");
  }
  const auto pos_accessor = find_number_after("\"POSITION\"", pos_attr);
  if (!pos_accessor) {
    return Err("failed to parse POSITION accessor");
  }
  std::optional<double> nrm_accessor;
  const auto nrm_attr = json.find("\"NORMAL\"", meshes_pos);
  if (nrm_attr != std::string::npos && nrm_attr < pos_attr + 200) {
    nrm_accessor = find_number_after("\"NORMAL\"", nrm_attr);
  }
  const auto indices_key = json.find("\"indices\"", meshes_pos);
  if (indices_key == std::string::npos) {
    return Err("GLB primitive missing indices");
  }
  const auto indices_accessor = find_number_after("\"indices\"", indices_key);
  if (!indices_accessor) {
    return Err("failed to parse indices accessor");
  }

  auto accessor_info = [&](int accessor_index, std::uint32_t& count, std::uint32_t& component_type,
                           std::uint64_t& byte_offset, int& buffer_view) -> bool {
    // Find the Nth object inside "accessors": [ {...}, {...} ]
    const auto accessors_pos = json.find("\"accessors\"");
    if (accessors_pos == std::string::npos) {
      return false;
    }
    auto arr = json.find('[', accessors_pos);
    if (arr == std::string::npos) {
      return false;
    }
    int depth = 0;
    int index = -1;
    std::size_t obj_start = std::string::npos;
    for (std::size_t i = arr; i < json.size(); ++i) {
      if (json[i] == '{') {
        if (depth == 0) {
          ++index;
          if (index == accessor_index) {
            obj_start = i;
          }
        }
        ++depth;
      } else if (json[i] == '}') {
        --depth;
        if (depth == 0 && obj_start != std::string::npos && index == accessor_index) {
          const std::string obj = json.substr(obj_start, i - obj_start + 1);
          auto num = [&](const char* key) -> std::optional<double> {
            const auto p = obj.find(key);
            if (p == std::string::npos) {
              return std::nullopt;
            }
            const auto c = obj.find(':', p);
            if (c == std::string::npos) {
              return std::nullopt;
            }
            return std::strtod(obj.c_str() + c + 1, nullptr);
          };
          count = static_cast<std::uint32_t>(num("\"count\"").value_or(0));
          component_type = static_cast<std::uint32_t>(num("\"componentType\"").value_or(0));
          byte_offset = static_cast<std::uint64_t>(num("\"byteOffset\"").value_or(0));
          buffer_view = static_cast<int>(num("\"bufferView\"").value_or(-1));
          return count > 0 && buffer_view >= 0;
        }
        if (depth == 0 && index > accessor_index) {
          return false;
        }
      } else if (json[i] == ']' && depth == 0) {
        break;
      }
    }
    return false;
  };

  auto buffer_view_offset = [&](int view_index, std::uint64_t& offset, std::uint64_t& length) -> bool {
    const auto views_pos = json.find("\"bufferViews\"");
    if (views_pos == std::string::npos) {
      return false;
    }
    auto arr = json.find('[', views_pos);
    int depth = 0;
    int index = -1;
    std::size_t obj_start = std::string::npos;
    for (std::size_t i = arr; i < json.size(); ++i) {
      if (json[i] == '{') {
        if (depth == 0) {
          ++index;
          if (index == view_index) {
            obj_start = i;
          }
        }
        ++depth;
      } else if (json[i] == '}') {
        --depth;
        if (depth == 0 && obj_start != std::string::npos && index == view_index) {
          const std::string obj = json.substr(obj_start, i - obj_start + 1);
          auto num = [&](const char* key) -> double {
            const auto p = obj.find(key);
            if (p == std::string::npos) {
              return 0.0;
            }
            const auto c = obj.find(':', p);
            return std::strtod(obj.c_str() + c + 1, nullptr);
          };
          offset = static_cast<std::uint64_t>(num("\"byteOffset\""));
          length = static_cast<std::uint64_t>(num("\"byteLength\""));
          return length > 0;
        }
      } else if (json[i] == ']' && depth == 0) {
        break;
      }
    }
    return false;
  };

  std::uint32_t pos_count = 0, pos_comp = 0;
  std::uint64_t pos_off = 0;
  int pos_view = -1;
  if (!accessor_info(static_cast<int>(*pos_accessor), pos_count, pos_comp, pos_off, pos_view)) {
    return Err("invalid POSITION accessor");
  }
  std::uint64_t view_off = 0, view_len = 0;
  if (!buffer_view_offset(pos_view, view_off, view_len) || bin.empty()) {
    return Err("POSITION bufferView missing BIN chunk");
  }
  const char* pos_ptr = bin.data() + view_off + pos_off;

  MeshCpu mesh;
  mesh.vertices.resize(pos_count);
  for (std::uint32_t i = 0; i < pos_count; ++i) {
    const float* p = reinterpret_cast<const float*>(pos_ptr + i * sizeof(float) * 3);
    mesh.vertices[i].position = {p[0], p[1], p[2]};
    mesh.vertices[i].normal = {0, 0, 1};
  }

  if (nrm_accessor) {
    std::uint32_t n_count = 0, n_comp = 0;
    std::uint64_t n_off = 0;
    int n_view = -1;
    if (accessor_info(static_cast<int>(*nrm_accessor), n_count, n_comp, n_off, n_view)) {
      std::uint64_t n_view_off = 0, n_view_len = 0;
      if (buffer_view_offset(n_view, n_view_off, n_view_len)) {
        const char* n_ptr = bin.data() + n_view_off + n_off;
        for (std::uint32_t i = 0; i < std::min(n_count, pos_count); ++i) {
          const float* n = reinterpret_cast<const float*>(n_ptr + i * sizeof(float) * 3);
          mesh.vertices[i].normal = {n[0], n[1], n[2]};
        }
      }
    }
  }

  std::uint32_t idx_count = 0, idx_comp = 0;
  std::uint64_t idx_off = 0;
  int idx_view = -1;
  if (!accessor_info(static_cast<int>(*indices_accessor), idx_count, idx_comp, idx_off, idx_view)) {
    return Err("invalid indices accessor");
  }
  std::uint64_t idx_view_off = 0, idx_view_len = 0;
  if (!buffer_view_offset(idx_view, idx_view_off, idx_view_len)) {
    return Err("indices bufferView missing");
  }
  const char* idx_ptr = bin.data() + idx_view_off + idx_off;
  mesh.indices.resize(idx_count);
  if (idx_comp == 5123) {  // UNSIGNED_SHORT
    for (std::uint32_t i = 0; i < idx_count; ++i) {
      mesh.indices[i] = reinterpret_cast<const std::uint16_t*>(idx_ptr)[i];
    }
  } else if (idx_comp == 5125) {  // UNSIGNED_INT
    for (std::uint32_t i = 0; i < idx_count; ++i) {
      mesh.indices[i] = reinterpret_cast<const std::uint32_t*>(idx_ptr)[i];
    }
  } else {
    return Err("unsupported index componentType");
  }

  recompute_bounds(mesh);
  log_info("Loaded GLB mesh");
  return mesh;
}

}  // namespace

MeshCpu make_demo_cube() {
  MeshCpu mesh;
  const Vec3 p[8] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                     {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
  // CCW when viewed from outside (outward normals).
  const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                           {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
  const Vec3 normals[6] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}};
  for (int f = 0; f < 6; ++f) {
    append_triangle(mesh, p[faces[f][0]], p[faces[f][1]], p[faces[f][2]], normals[f]);
    append_triangle(mesh, p[faces[f][0]], p[faces[f][2]], p[faces[f][3]], normals[f]);
  }
  recompute_bounds(mesh);
  return mesh;
}

Result<MeshCpu> load_obj(const std::filesystem::path& path) {
  auto result = rapidobj::ParseFile(path);
  if (result.error) {
    return Err("OBJ load failed: " + result.error.code.message());
  }
  if (!rapidobj::Triangulate(result)) {
    return Err("OBJ triangulate failed");
  }

  MeshCpu mesh;
  const auto& positions = result.attributes.positions;
  const auto& normals = result.attributes.normals;
  for (const auto& shape : result.shapes) {
    std::size_t index_offset = 0;
    for (std::size_t fi = 0; fi < shape.mesh.num_face_vertices.size(); ++fi) {
      const auto fv = shape.mesh.num_face_vertices[fi];
      if (fv != 3) {
        index_offset += fv;
        continue;
      }
      Vertex tri[3]{};
      for (std::size_t v = 0; v < 3; ++v) {
        const auto idx = shape.mesh.indices[index_offset + v];
        tri[v].position = {positions[static_cast<std::size_t>(idx.position_index) * 3 + 0],
                           positions[static_cast<std::size_t>(idx.position_index) * 3 + 1],
                           positions[static_cast<std::size_t>(idx.position_index) * 3 + 2]};
        if (idx.normal_index >= 0) {
          tri[v].normal = {normals[static_cast<std::size_t>(idx.normal_index) * 3 + 0],
                           normals[static_cast<std::size_t>(idx.normal_index) * 3 + 1],
                           normals[static_cast<std::size_t>(idx.normal_index) * 3 + 2]};
        }
      }
      if (length(tri[0].normal) < 1e-6f) {
        const Vec3 n = normalize(cross(tri[1].position - tri[0].position,
                                       tri[2].position - tri[0].position));
        tri[0].normal = tri[1].normal = tri[2].normal = n;
      }
      const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
      mesh.vertices.push_back(tri[0]);
      mesh.vertices.push_back(tri[1]);
      mesh.vertices.push_back(tri[2]);
      mesh.indices.push_back(base);
      mesh.indices.push_back(base + 1);
      mesh.indices.push_back(base + 2);
      index_offset += fv;
    }
  }
  if (mesh.indices.empty()) {
    return Err("OBJ contained no triangles");
  }
  recompute_bounds(mesh);
  return mesh;
}

Result<MeshCpu> load_gltf(const std::filesystem::path& path) {
  auto ext = path.extension().string();
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (ext == ".glb") {
    return load_glb(path);
  }
  return Err("ASCII .gltf is not supported yet; please use .glb or .obj");
}

Result<MeshCpu> load_mesh_file(const std::filesystem::path& path) {
  auto ext = path.extension().string();
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (ext == ".obj") {
    return load_obj(path);
  }
  if (ext == ".gltf" || ext == ".glb") {
    return load_gltf(path);
  }
  return Err("unsupported mesh format: " + ext);
}

}  // namespace tamias
