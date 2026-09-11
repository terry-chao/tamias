#include "engine/render/scene_debug_player.h"

#include <algorithm>

namespace tamias {

void apply_render_scene_view(TurntableCamera& camera, const RenderScene::View& view) {
  camera.set_target(view.target);
  camera.set_distance(view.view_distance);
  camera.set_yaw_pitch(view.yaw, view.pitch);
  camera.set_fovy(view.fovy);
  camera.set_znear(view.znear);
  camera.set_zfar(view.zfar);
  camera.set_orthographic(view.orthographic);
}

void fill_aabb_debug_lines(const Aabb& box, std::vector<Vec3>& lines) {
  if (!box.valid()) {
    return;
  }
  const Vec3 c[8] = {
      {box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z},
      {box.min.x, box.max.y, box.min.z}, {box.max.x, box.max.y, box.min.z},
      {box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z},
      {box.min.x, box.max.y, box.max.z}, {box.max.x, box.max.y, box.max.z},
  };
  const int edges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                            {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  lines.reserve(lines.size() + 24);
  for (const auto& e : edges) {
    lines.push_back(c[e[0]]);
    lines.push_back(c[e[1]]);
  }
}

void SceneDebugPlayer::bump_generation() { ++generation_; }

void SceneDebugPlayer::set_scene(RenderScene scene) {
  scene_ = std::move(scene);
  std::sort(scene_.hidden_node_ids.begin(), scene_.hidden_node_ids.end());
  scene_.hidden_node_ids.erase(
      std::unique(scene_.hidden_node_ids.begin(), scene_.hidden_node_ids.end()),
      scene_.hidden_node_ids.end());
  has_scene_ = true;
  isolate_node_.reset();
  step_count_.reset();
  debug_aabb_.reset();
  debug_vertex_.reset();
  debug_triangle_.reset();
  bump_generation();
}

void SceneDebugPlayer::set_isolate_node(std::optional<std::uint64_t> node_id) {
  if (isolate_node_ == node_id) {
    return;
  }
  isolate_node_ = node_id;
  bump_generation();
}

void SceneDebugPlayer::set_step_count(std::optional<std::size_t> count) {
  if (step_count_ == count) {
    return;
  }
  step_count_ = count;
  bump_generation();
}

void SceneDebugPlayer::set_debug_aabb(std::optional<Aabb> box) { debug_aabb_ = std::move(box); }

void SceneDebugPlayer::set_debug_vertex(std::optional<DebugVertexOverlay> vertex) {
  debug_vertex_ = std::move(vertex);
}

void SceneDebugPlayer::set_debug_triangle(std::optional<std::array<Vec3, 3>> triangle) {
  debug_triangle_ = std::move(triangle);
}

std::vector<SceneDrawItem> SceneDebugPlayer::filtered_items() const {
  std::vector<SceneDrawItem> out;
  if (!has_scene_) {
    return out;
  }
  out.reserve(scene_.items.size());
  for (const auto& item : scene_.items) {
    if (isolate_node_ && item.node_id != *isolate_node_) {
      continue;
    }
    out.push_back(item);
    if (step_count_ && out.size() >= *step_count_) {
      break;
    }
  }
  return out;
}

std::vector<std::uint64_t> SceneDebugPlayer::hidden_node_ids() const {
  if (!apply_hidden_ || !has_scene_) {
    return {};
  }
  return scene_.hidden_node_ids;
}

Aabb SceneDebugPlayer::bounds() const {
  Aabb box;
  if (!has_scene_) {
    return box;
  }
  const auto expand_box = [&](const Aabb& other) {
    if (!other.valid()) {
      return;
    }
    box.expand(other.min);
    box.expand(other.max);
  };
  for (const auto& item : scene_.items) {
    expand_box(item.bounds);
  }
  if (!box.valid()) {
    for (const auto& [id, mesh] : scene_.meshes) {
      (void)id;
      expand_box(mesh.bounds);
    }
  }
  return box;
}

FrameSubmission SceneDebugPlayer::make_frame(NativeWindowHandle window, std::uint32_t width,
                                             std::uint32_t height, const TurntableCamera& camera,
                                             RenderMode mode) const {
  FrameSubmission frame{};
  frame.window = window;
  frame.width = std::max(1u, width);
  frame.height = std::max(1u, height);
  const float aspect =
      static_cast<float>(frame.width) / static_cast<float>(std::max(1u, frame.height));
  frame.view = camera.view_matrix();
  frame.proj = camera.proj_matrix(aspect);
  frame.eye_position = camera.eye_position();
  frame.view_distance = camera.distance();
  frame.fovy = camera.fovy();
  frame.mode = mode;
  frame.items = filtered_items();
  frame.hidden_node_ids = hidden_node_ids();
  frame.scene_generation = generation_;
  frame.show_axes = show_axes_;
  frame.debug_vertex = debug_vertex_;
  if (debug_aabb_ && debug_aabb_->valid()) {
    fill_aabb_debug_lines(*debug_aabb_, frame.debug_line_segments);
  }
  if (debug_triangle_) {
    const auto& t = *debug_triangle_;
    frame.debug_line_segments.push_back(t[0]);
    frame.debug_line_segments.push_back(t[1]);
    frame.debug_line_segments.push_back(t[1]);
    frame.debug_line_segments.push_back(t[2]);
    frame.debug_line_segments.push_back(t[2]);
    frame.debug_line_segments.push_back(t[0]);
  }
  return frame;
}

}  // namespace tamias
