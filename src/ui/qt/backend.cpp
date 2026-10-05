#include "ui/qt/backend.h"

#include "ui/qt/dialogs.h"
#include "ui/qt/text.h"
#include "ui/qt/theme.h"

#include <QtGlobal>

#include <memory>

namespace tac::qt {
namespace {

class QtPlatformServices final : public PlatformServices {
 public:
  [[nodiscard]] DialogService& dialogs() override { return dialogs_; }
  [[nodiscard]] Theme& theme() override { return theme_; }
  [[nodiscard]] TextMeasurer& text() override { return text_; }

  QtDialogService& dialog_service() { return dialogs_; }

 private:
  QtDialogService dialogs_;
  QtTheme theme_;
  QtTextMeasurer text_;
};

class QtBackend final : public UiBackend {
 public:
  QtBackend() : info_{"qt", QT_VERSION_STR} {}

  [[nodiscard]] const BackendInfo& info() const override { return info_; }
  [[nodiscard]] PlatformServices& platform() override { return services_; }
  [[nodiscard]] QtDialogService& dialogs() { return services_.dialog_service(); }

  // 窗口与渲染表面还在迁移计划里（docs/TAC.md 阶段 3 / 4）：
  // 现在明确返回 nullptr，调用方必须处理"后端暂不支持"这条路径。
  std::unique_ptr<Window> create_window(const WindowDesc& /*desc*/) override { return nullptr; }
  std::unique_ptr<ViewportSurface> create_surface(const SurfaceDesc& /*desc*/) override {
    return nullptr;
  }

 private:
  BackendInfo info_;
  QtPlatformServices services_;
};

}  // namespace

std::unique_ptr<UiBackend> make_backend() { return std::make_unique<QtBackend>(); }

void register_qt_backend() { register_backend("qt", &make_backend); }

}  // namespace tac::qt
