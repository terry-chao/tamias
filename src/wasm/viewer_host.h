#pragma once

#include "engine/core/native_window_handle.h"
#include "engine/core/result.h"
#include "engine/render/render_runtime.h"
#include "host/session.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {

// 桌面视口左下角那行读数（draw / tri / gpu / tess）在 web 上也要有。
struct ViewerStats {
  int draws = 0;
  int triangles = 0;
  double gpu_mesh_mb = 0.0;
  int pending_tessellate = 0;
};

// WASM 宿主：Session + 渲染通道 + canvas 管理。
// 文档 / 命令 / 相机 / 选择都走 Session；embind 面 = Session 能力 1:1。
class ViewerHost {
 public:
  ViewerHost();
  ~ViewerHost();

  Result<void> start(const char* canvas_selector);
  Result<void> load_bytes(std::string_view name, std::span<const std::uint8_t> bytes);
  // 新建文档：清空当前内容，填一个内置示例场景（走命令层，几何由已注册内核求值）。
  bool new_document();
  void resize(std::uint32_t width, std::uint32_t height);
  // 视图模式：0 线框 / 1 着色 / 2 真实感（与 RenderMode 同序，桌面端是同一套）。
  void set_render_mode(int mode);
  [[nodiscard]] int render_mode() const { return static_cast<int>(mode_); }
  // 相机朝向，与桌面 ViewCube 同一套约定（Front=+Z, Right=+X, Top=+Y）：
  // eye_dir = (cos(pitch)·sin(yaw), sin(pitch), cos(pitch)·cos(yaw))。
  void set_view_angles(double yaw, double pitch);
  [[nodiscard]] double view_yaw() const;
  [[nodiscard]] double view_pitch() const;
  // 点选：视口归一化坐标 → 命中的节点 id；未命中返回 0 并清空选择。
  std::uint64_t pick_entity(float nx, float ny);
  [[nodiscard]] ViewerStats stats() const;
  // 引擎日志里最近的问题（warn / error），给页面上的错误面板用。
  [[nodiscard]] std::string log_text() const;
  void clear_log();
  void pointer_down(float x, float y, int button);
  void pointer_move(float x, float y);
  void pointer_up(float x, float y, int button);
  void wheel(float delta_y);
  void frame_all();
  void render();
  // 视口归一化坐标 (nx, ny ∈ [0,1]) 打到 y = plane_y 水平面上的世界点，
  // 返回 "x,y,z"；射线与平面平行或交点在相机背后时返回空串。
  [[nodiscard]] std::string pick_work_plane(float nx, float ny, float plane_y) const;
  [[nodiscard]] const std::string& status() const { return status_; }
  [[nodiscard]] std::string document_name() const;

  // Session 能力（embind 直出）。
  [[nodiscard]] bool dispatch(std::string_view command, std::string_view args_text);
  void undo();
  void redo();
  [[nodiscard]] bool can_undo() const { return session_->can_undo(); }
  [[nodiscard]] bool can_redo() const { return session_->can_redo(); }
  [[nodiscard]] std::vector<std::uint64_t> selection() const { return session_->selection(); }
  void clear_selection() { session_->clear_selection(); }

 private:
  NativeWindowHandle window() const;
  void upload_document();
  void load_demo();

  std::unique_ptr<Session> session_;
  std::shared_ptr<RenderThread> render_thread_;
  std::unique_ptr<RenderChannel> channel_;
  mutable std::mutex log_mutex_;
  std::vector<std::string> log_lines_;
  std::string canvas_selector_ = "#viewport";
  std::string status_ = "idle";
  std::uint32_t width_ = 1;
  std::uint32_t height_ = 1;
  float last_x_ = 0.f;
  float last_y_ = 0.f;
  bool orbiting_ = false;
  bool panning_ = false;
  bool loaded_ = false;
  RenderMode mode_ = RenderMode::Shaded;
  std::uint64_t last_submitted_scene_generation_ = 0;  // 脏标记游标（见 Scene::dirty_since）
};

}  // namespace tamias
