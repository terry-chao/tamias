#pragma once

#include "engine/core/native_window_handle.h"
#include "engine/core/result.h"
#include "engine/render/render_runtime.h"
#include "host/session.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {

// WASM 宿主：Session + 渲染通道 + canvas 管理。
// 文档 / 命令 / 相机 / 选择都走 Session；embind 面 = Session 能力 1:1。
class ViewerHost {
 public:
  ViewerHost();
  ~ViewerHost();

  Result<void> start(const char* canvas_selector);
  Result<void> load_bytes(std::string_view name, std::span<const std::uint8_t> bytes);
  void resize(std::uint32_t width, std::uint32_t height);
  void pointer_down(float x, float y, int button);
  void pointer_move(float x, float y);
  void pointer_up(float x, float y, int button);
  void wheel(float delta_y);
  void frame_all();
  void render();
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
  std::string canvas_selector_ = "#viewport";
  std::string status_ = "idle";
  std::uint32_t width_ = 1;
  std::uint32_t height_ = 1;
  float last_x_ = 0.f;
  float last_y_ = 0.f;
  bool orbiting_ = false;
  bool panning_ = false;
  bool loaded_ = false;
};

}  // namespace tamias
