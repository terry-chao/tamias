#pragma once

#include "engine/core/result.h"
#include "engine/graphics/mesh.h"
#include "engine/io/imported_model.h"

#include <filesystem>
#include <span>
#include <string_view>

namespace tamias {

Result<MeshCpu> load_obj(const std::filesystem::path& path);
Result<MeshCpu> load_obj_bytes(std::span<const std::byte> bytes);
Result<MeshCpu> load_gltf(const std::filesystem::path& path);
Result<MeshCpu> load_mesh_file(const std::filesystem::path& path);
Result<ImportedModel> load_obj_model(const std::filesystem::path& path);
Result<ImportedModel> load_gltf_model(const std::filesystem::path& path);
Result<ImportedModel> load_mesh_model(const std::filesystem::path& path);
Result<MeshCpu> load_mesh_bytes(std::span<const std::byte> bytes, std::string_view extension);

Result<void> save_obj(const std::filesystem::path& path, const MeshCpu& mesh);
Result<void> save_mesh_file(const std::filesystem::path& path, const MeshCpu& mesh);

MeshCpu make_demo_cube();

}  // namespace tamias
