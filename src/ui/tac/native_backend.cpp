#include "ui/tac/native_backend.h"

#include "ui/tac/backend.h"

#include <memory>
#include <optional>
#include <string_view>

namespace tac {
namespace {

// 骨架平台服务：契约里的能力先明确"暂不支持"，让调用方拿到可判定的空结果，
// 而不是静默假装成功。阶段 5 自研控件落地后逐个换成真实现。
class SkeletonDialogs final : public DialogService {
 public:
  DialogResult show_message(std::string_view /*title*/, std::string_view /*message*/,
                            DialogButtons /*buttons*/) override {
    return DialogResult::None;
  }
  std::optional<std::string> prompt_string(std::string_view /*title*/, std::string_view /*label*/,
                                           std::string_view /*default_value*/) override {
    return std::nullopt;
  }
  std::optional<double> prompt_number(std::string_view /*title*/, std::string_view /*label*/,
                                      double /*default_value*/, std::optional<double> /*min*/,
                                      std::optional<double> /*max*/) override {
    return std::nullopt;
  }
  bool show_form(PromptForm& /*form*/) override { return false; }
  std::optional<std::string> open_file(const FileDialogRequest& /*request*/) override {
    return std::nullopt;
  }
  std::optional<std::string> save_file(const FileDialogRequest& /*request*/) override {
    return std::nullopt;
  }
};

class SkeletonTheme final : public Theme {
 public:
  [[nodiscard]] bool is_dark() const override { return false; }
  [[nodiscard]] ThemePalette palette() const override { return {}; }
};

class SkeletonText final : public TextMeasurer {
 public:
  [[nodiscard]] FontMetrics measure(std::string_view /*utf8*/, const FontSpec& /*font*/) const
      override {
    return {};
  }
};

class SkeletonServices final : public PlatformServices {
 public:
  [[nodiscard]] DialogService& dialogs() override { return dialogs_; }
  [[nodiscard]] Theme& theme() override { return theme_; }
  [[nodiscard]] TextMeasurer& text() override { return text_; }

 private:
  SkeletonDialogs dialogs_;
  SkeletonTheme theme_;
  SkeletonText text_;
};

class NativeBackend final : public UiBackend {
 public:
  NativeBackend() : info_{"tac", "0.0.1-skeleton"} {}

  [[nodiscard]] const BackendInfo& info() const override { return info_; }
  [[nodiscard]] PlatformServices& platform() override { return services_; }

  // 窗口与渲染表面属于阶段 3 / 4：现在按契约返回 nullptr，表示"暂不支持"。
  std::unique_ptr<Window> create_window(const WindowDesc& /*desc*/) override { return nullptr; }
  std::unique_ptr<ViewportSurface> create_surface(const SurfaceDesc& /*desc*/) override {
    return nullptr;
  }

 private:
  BackendInfo info_;
  SkeletonServices services_;
};

std::unique_ptr<UiBackend> make_native_backend() {
  return std::make_unique<NativeBackend>();
}

}  // namespace

void register_native_backend() { register_backend("tac", &make_native_backend); }

}  // namespace tac
