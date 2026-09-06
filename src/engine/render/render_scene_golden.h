#pragma once

#include "engine/core/result.h"
#include "engine/render/render_scene.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {

// 仓库金样 sidecar：digest 放这里，不要写进 C++ 字符串。
struct RenderSceneGoldenMeta {
  std::string name;
  std::string digest;
  std::uint64_t items = 0;
  std::uint64_t meshes = 0;
  std::uint64_t textures = 0;
  int mode = 1;  // RenderMode 数值
};

[[nodiscard]] std::filesystem::path render_scene_golden_root(
    const std::filesystem::path& source_dir);

[[nodiscard]] bool is_render_scene_golden_slug(std::string_view slug);
[[nodiscard]] std::string suggest_render_scene_golden_slug(std::string_view name);

[[nodiscard]] RenderSceneGoldenMeta make_render_scene_golden_meta(std::string name,
                                                                  const RenderScene& scene);

[[nodiscard]] std::vector<std::filesystem::path> list_render_scene_goldens(
    const std::filesystem::path& golden_root);

Result<RenderSceneGoldenMeta> load_render_scene_golden_meta(const std::filesystem::path& dir);

// 已有 scene.trscn 时补写 sidecar（不改 trscn）。
Result<RenderSceneGoldenMeta> refresh_render_scene_golden_sidecar(const std::filesystem::path& dir);

// 写出 scene.trscn / scene.meta.json / scene.inspect.txt / debug/，再 load 校验 digest。
// 不改调用方的 Document 路径。已存在且 overwrite=false 时失败。
Result<RenderSceneGoldenMeta> save_render_scene_golden(const std::filesystem::path& golden_root,
                                                       std::string_view slug,
                                                       const RenderScene& scene, bool overwrite);

}  // namespace tamias
