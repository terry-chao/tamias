#pragma once

#include "ui/tac/platform/services.h"
#include "ui/tac/platform/window.h"
#include "ui/tac/surface.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tac {

struct BackendInfo {
  std::string name;     // "qt" / "tac" / ...
  std::string version;
};

// 界面后端：一套界面库要插进来的接口。壳只依赖这个抽象，不依赖 Qt。
class UiBackend {
 public:
  virtual ~UiBackend() = default;

  [[nodiscard]] virtual const BackendInfo& info() const = 0;
  [[nodiscard]] virtual PlatformServices& platform() = 0;

  // 返回 nullptr 表示后端当前不支持该能力（迁移期各阶段逐步补齐）。
  virtual std::unique_ptr<Window> create_window(const WindowDesc& desc) = 0;
  virtual std::unique_ptr<ViewportSurface> create_surface(const SurfaceDesc& desc) = 0;
};

using BackendFactory = std::unique_ptr<UiBackend> (*)();

// 后端登记表。显式登记而不是静态初始化，避免静态初始化顺序问题：
// main() 里调 tac::qt::register_qt_backend() 之后才能 create_backend("qt")。
void register_backend(std::string_view name, BackendFactory factory);

[[nodiscard]] std::unique_ptr<UiBackend> create_backend(std::string_view name);
[[nodiscard]] std::vector<std::string> backend_names();

// 按名字取后端；取不到就退到 fallback。used 非空时写回实际用的名字
// （两个都取不到则为空串）。界面的默认与回退都应是 "qt"。
[[nodiscard]] std::unique_ptr<UiBackend> create_backend_or_fallback(
    std::string_view preferred, std::string_view fallback, std::string* used = nullptr);

}  // namespace tac
