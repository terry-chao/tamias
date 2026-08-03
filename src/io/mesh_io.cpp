#include "mesh_io.h"

#include "core/log.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 5311) // rapidobj: deprecated `operator"" _suffix` spacing
#endif
#include <rapidobj/rapidobj.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <optional>
#include <string_view>
#include <vector>

namespace tamias {
namespace {

void append_triangle(MeshCpu& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 n) {
  const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
  Vertex va{};
  va.position = a;
  va.normal = n;
  Vertex vb = va;
  vb.position = b;
  Vertex vc = va;
  vc.position = c;
  mesh.vertices.push_back(va);
  mesh.vertices.push_back(vb);
  mesh.vertices.push_back(vc);
  mesh.indices.push_back(base);
  mesh.indices.push_back(base + 1);
  mesh.indices.push_back(base + 2);
}

constexpr Vec3 kDefaultMeshColor{0.75f, 0.78f, 0.82f};

// Find the start index of the Nth `{...}` object inside a JSON array after `array_key`.
std::optional<std::string> nth_json_object(const std::string& json, std::string_view array_key,
                                           int object_index) {
  if (object_index < 0) {
    return std::nullopt;
  }
  const auto key_pos = json.find(array_key);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  const auto arr = json.find('[', key_pos + array_key.size());
  if (arr == std::string::npos) {
    return std::nullopt;
  }
  int depth = 0;
  int index = -1;
  std::size_t obj_start = std::string::npos;
  for (std::size_t i = arr; i < json.size(); ++i) {
    if (json[i] == '{') {
      if (depth == 0) {
        ++index;
        if (index == object_index) {
          obj_start = i;
        }
      }
      ++depth;
    } else if (json[i] == '}') {
      --depth;
      if (depth == 0 && obj_start != std::string::npos && index == object_index) {
        return json.substr(obj_start, i - obj_start + 1);
      }
    } else if (json[i] == ']' && depth == 0) {
      break;
    }
  }
  return std::nullopt;
}

Vec3 parse_base_color_factor(const std::string& material_json) {
  const auto pbr = material_json.find("\"pbrMetallicRoughness\"");
  if (pbr == std::string::npos) {
    return {1.f, 1.f, 1.f};
  }
  const auto factor = material_json.find("\"baseColorFactor\"", pbr);
  if (factor == std::string::npos) {
    return {1.f, 1.f, 1.f};
  }
  const auto bracket = material_json.find('[', factor);
  if (bracket == std::string::npos) {
    return {1.f, 1.f, 1.f};
  }
  char* end = nullptr;
  const float r = static_cast<float>(std::strtod(material_json.c_str() + bracket + 1, &end));
  if (!end) {
    return {1.f, 1.f, 1.f};
  }
  while (*end == ' ' || *end == ',') {
    ++end;
  }
  const float g = static_cast<float>(std::strtod(end, &end));
  while (end && (*end == ' ' || *end == ',')) {
    ++end;
  }
  const float b = static_cast<float>(std::strtod(end, &end));
  return {r, g, b};
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

  Vec3 base_color{1.f, 1.f, 1.f};
  // Look for "material" on the first primitive (near POSITION / indices).
  const auto prim_window_end = std::min(json.size(), indices_key + 120);
  const auto material_key = json.find("\"material\"", meshes_pos);
  if (material_key != std::string::npos && material_key < prim_window_end) {
    if (const auto mat_idx = find_number_after("\"material\"", material_key)) {
      if (auto mat_obj = nth_json_object(json, "\"materials\"", static_cast<int>(*mat_idx))) {
        base_color = parse_base_color_factor(*mat_obj);
      }
    }
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
    mesh.vertices[i].color = base_color;
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
  // Optional MTL so samples without companion .mtl still load.
  auto result = rapidobj::ParseFile(path, rapidobj::MaterialLibrary::Default(rapidobj::Load::Optional));
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
      Vec3 face_color = kDefaultMeshColor;
      if (fi < shape.mesh.material_ids.size()) {
        const int mid = shape.mesh.material_ids[fi];
        if (mid >= 0 && static_cast<std::size_t>(mid) < result.materials.size()) {
          const auto& kd = result.materials[static_cast<std::size_t>(mid)].diffuse;
          face_color = {kd[0], kd[1], kd[2]};
        }
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
        tri[v].color = face_color;
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

Result<void> save_obj(const std::filesystem::path& path, const MeshCpu& mesh) {
  if (mesh.indices.empty() || mesh.vertices.empty()) {
    return Err("mesh has no triangles to save");
  }
  if (mesh.indices.size() % 3 != 0) {
    return Err("mesh index count is not a multiple of 3");
  }

  std::ofstream out(path);
  if (!out) {
    return Err("failed to open file for writing: " + path.string());
  }

  out << "# Tamias OBJ export\n";
  out << "o mesh\n";
  out << std::setprecision(9) << std::fixed;

  const bool write_colors = mesh_has_vertex_colors(mesh);
  for (const auto& v : mesh.vertices) {
    out << "v " << v.position.x << ' ' << v.position.y << ' ' << v.position.z;
    if (write_colors) {
      out << ' ' << v.color.x << ' ' << v.color.y << ' ' << v.color.z;
    }
    out << '\n';
  }
  for (const auto& v : mesh.vertices) {
    out << "vn " << v.normal.x << ' ' << v.normal.y << ' ' << v.normal.z << '\n';
  }
  for (const auto& v : mesh.vertices) {
    out << "vt " << v.uv.x << ' ' << v.uv.y << '\n';
  }
  for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const auto i0 = mesh.indices[i] + 1;
    const auto i1 = mesh.indices[i + 1] + 1;
    const auto i2 = mesh.indices[i + 2] + 1;
    out << "f " << i0 << '/' << i0 << '/' << i0 << ' ' << i1 << '/' << i1 << '/' << i1 << ' '
        << i2 << '/' << i2 << '/' << i2 << '\n';
  }
  if (!out) {
    return Err("failed while writing OBJ: " + path.string());
  }
  return {};
}

Result<void> save_mesh_file(const std::filesystem::path& path, const MeshCpu& mesh) {
  auto ext = path.extension().string();
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (ext == ".obj") {
    return save_obj(path, mesh);
  }
  return Err("unsupported mesh write format: " + ext + " (only .obj is supported)");
}

}  // namespace tamias
