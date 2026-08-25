#pragma once

#include "engine/core/native_window_handle.h"
#include "engine/core/result.h"
#include "engine/document/document.h"
#include "engine/math/camera.h"
#include "engine/render/render_runtime.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace tamias {

// Non-Qt host: loads .tdoc / OBJ from memory and presents through a WebGL canvas.
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

 private:
  NativeWindowHandle window() const;
  void upload_document();
  void load_demo();

  std::shared_ptr<Document> document_;
  std::shared_ptr<RenderThread> render_thread_;
  std::unique_ptr<RenderChannel> channel_;
  TurntableCamera camera_;
  std::string canvas_selector_ = "#viewport";
  std::string status_ = "idle";
  std::uint32_t width_ = 1;
  std::uint32_t height_ = 1;
  float last_x_ = 0.f;
  float last_y_ = 0.f;
  bool orbiting_ = false;
  bool panning_ = false;
};

}  // namespace tamias
