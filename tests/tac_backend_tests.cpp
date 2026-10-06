// tac 后端登记表：默认/回退/缺失的行为（见 docs/TAC.md §6）。

#include "ui/tac/backend.h"
#include "ui/tac/native_backend.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

// 一个什么都不做的后端，只为把登记表跑通（tac 契约全是虚接口）。
struct StubDialogs final : tac::DialogService {
  tac::DialogResult show_message(std::string_view, std::string_view,
                                 tac::DialogButtons) override {
    return tac::DialogResult::None;
  }
  std::optional<std::string> prompt_string(std::string_view, std::string_view,
                                           std::string_view) override {
    return std::nullopt;
  }
  std::optional<double> prompt_number(std::string_view, std::string_view, double,
                                      std::optional<double>, std::optional<double>) override {
    return std::nullopt;
  }
  bool show_form(tac::PromptForm&) override { return false; }
  std::optional<std::string> open_file(const tac::FileDialogRequest&) override {
    return std::nullopt;
  }
  std::optional<std::string> save_file(const tac::FileDialogRequest&) override {
    return std::nullopt;
  }
};

struct StubTheme final : tac::Theme {
  [[nodiscard]] bool is_dark() const override { return false; }
  [[nodiscard]] tac::ThemePalette palette() const override { return {}; }
};

struct StubText final : tac::TextMeasurer {
  [[nodiscard]] tac::FontMetrics measure(std::string_view, const tac::FontSpec&) const override {
    return {};
  }
};

struct StubServices final : tac::PlatformServices {
  [[nodiscard]] tac::DialogService& dialogs() override { return dialogs_; }
  [[nodiscard]] tac::Theme& theme() override { return theme_; }
  [[nodiscard]] tac::TextMeasurer& text() override { return text_; }

  StubDialogs dialogs_;
  StubTheme theme_;
  StubText text_;
};

struct StubBackend final : tac::UiBackend {
  [[nodiscard]] const tac::BackendInfo& info() const override { return info_; }
  [[nodiscard]] tac::PlatformServices& platform() override { return services_; }
  std::unique_ptr<tac::Window> create_window(const tac::WindowDesc&) override { return nullptr; }
  std::unique_ptr<tac::ViewportSurface> create_surface(const tac::SurfaceDesc&) override {
    return nullptr;
  }

  tac::BackendInfo info_{"stub", "1"};
  StubServices services_;
};

std::unique_ptr<tac::UiBackend> make_stub() {
  return std::make_unique<StubBackend>();
}

}  // namespace

TEST(TacBackend, MissingBackendIsNull) {
  EXPECT_EQ(tac::create_backend("tac-backend-does-not-exist"), nullptr);
}

TEST(TacBackend, RegistryListsWhatWasRegistered) {
  tac::register_backend("tac-test-registry", &make_stub);
  const auto names = tac::backend_names();
  EXPECT_NE(std::find(names.begin(), names.end(), "tac-test-registry"), names.end());
}

TEST(TacBackend, FallsBackWhenPreferredIsMissing) {
  tac::register_backend("tac-test-fallback", &make_stub);
  std::string used;
  auto backend = tac::create_backend_or_fallback("tac-nope", "tac-test-fallback", &used);
  ASSERT_NE(backend, nullptr);
  EXPECT_EQ(used, "tac-test-fallback");
}

TEST(TacBackend, UsesPreferredWhenAvailable) {
  tac::register_backend("tac-test-preferred", &make_stub);
  std::string used;
  auto backend = tac::create_backend_or_fallback("tac-test-preferred", "tac-nope", &used);
  ASSERT_NE(backend, nullptr);
  EXPECT_EQ(used, "tac-test-preferred");
}

TEST(TacBackend, ReportsEmptyWhenNothingIsAvailable) {
  std::string used = "sentinel";
  auto backend = tac::create_backend_or_fallback("tac-nope-a", "tac-nope-b", &used);
  EXPECT_EQ(backend, nullptr);
  EXPECT_TRUE(used.empty());
}

TEST(TacBackend, RegisteredFactoryCanDecline) {
  tac::register_backend("tac-test-declines", [] { return std::unique_ptr<tac::UiBackend>(); });
  EXPECT_EQ(tac::create_backend("tac-test-declines"), nullptr);
}

// 设置里要能列出并选中 tac，所以 "tac" 必须是登记过的后端（骨架也算）。
TEST(TacBackend, NativeBackendIsListedAndSelectable) {
  tac::register_native_backend();
  const auto names = tac::backend_names();
  EXPECT_NE(std::find(names.begin(), names.end(), "tac"), names.end());

  auto backend = tac::create_backend("tac");
  ASSERT_NE(backend, nullptr);
  EXPECT_EQ(backend->info().name, "tac");
  // 骨架阶段：窗口与画布明确"暂不支持"，而不是假装能用。
  EXPECT_EQ(backend->create_window(tac::WindowDesc{}), nullptr);
  EXPECT_EQ(backend->create_surface(tac::SurfaceDesc{}), nullptr);
}
