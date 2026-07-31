#pragma once

#include "core/result.h"
#include "graphics/mesh.h"

#include <filesystem>

namespace tamias {

Result<MeshCpu> load_obj(const std::filesystem::path& path);
Result<MeshCpu> load_gltf(const std::filesystem::path& path);
Result<MeshCpu> load_mesh_file(const std::filesystem::path& path);

Result<void> save_obj(const std::filesystem::path& path, const MeshCpu& mesh);
Result<void> save_mesh_file(const std::filesystem::path& path, const MeshCpu& mesh);

MeshCpu make_demo_cube();

}  // namespace tamias
