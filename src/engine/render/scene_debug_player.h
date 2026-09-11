#pragma once

#include "engine/math/camera.h"
#include "engine/render/debug_vertex_overlay.h"
#include "engine/render/render_runtime.h"
#include "engine/render/render_scene.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace tamias {

// 把烤好的 RenderScene 直接变成 FrameSubmission，不经过 Document。
class SceneDebugPlayer {
 public:
  void set_scene(RenderScene scene);
  [[nodiscard]] const RenderScene& scene() const { return scene_; }
  [[nodiscard]] bool has_scene() const { return has_scene_; }

  void set_isolate_node(std::optional<std::uint64_t> node_id);
  [[nodiscard]] std::optional<std::uint64_t> isolate_node() const { return isolate_node_; }

  // 保留过滤后的前 N 条；nullopt = 全部。
  void set_step_count(std::optional<std::size_t> count);
  [[nodiscard]] std::optional<std::size_t> step_count() const { return step_count_; }

  void set_apply_captured_hidden(bool enabled) { apply_hidden_ = enabled; }
  [[nodiscard]] bool apply_captured_hidden() const { return apply_hidden_; }

  void set_debug_aabb(std::optional<Aabb> box);
  void set_debug_vertex(std::optional<DebugVertexOverlay> vertex);
  void set_show_axes(bool enabled) { show_axes_ = enabled; }
  [[nodiscard]] bool show_axes() const { return show_axes_; }

  [[nodiscard]] std::vector<SceneDrawItem> filtered_items() const;
  [[nodiscard]] std::vector<std::uint64_t> hidden_node_ids() const;
  [[nodiscard]] Aabb bounds() const;
  [[nodiscard]] std::uint64_t generation() const { return generation_; }

  [[nodiscard]] FrameSubmission make_frame(NativeWindowHandle window, std::uint32_t width,
                                           std::uint32_t height, const TurntableCamera& camera,
                                           RenderMode mode) const;

 private:
  void bump_generation();

  RenderScene scene_{};
  bool has_scene_ = false;
  std::optional<std::uint64_t> isolate_node_;
  std::optional<std::size_t> step_count_;
  bool apply_hidden_ = true;
  bool show_axes_ = false;
  std::optional<Aabb> debug_aabb_;
  std::optional<DebugVertexOverlay> debug_vertex_;
  std::uint64_t generation_ = 1;
};

void apply_render_scene_view(TurntableCamera& camera, const RenderScene::View& view);
void fill_aabb_debug_lines(const Aabb& box, std::vector<Vec3>& lines);

}  // namespace tamias
