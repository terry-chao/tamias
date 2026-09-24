#include "engine/render/rhi/device.h"

#if defined(TAMIAS_HAS_RHI_VULKAN)
#include "engine/render/rhi/vulkan/vulkan_backend.h"
#endif
#if defined(TAMIAS_HAS_RHI_OPENGL)
#include "engine/render/rhi/opengl/opengl_backend.h"
#endif

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace tamias {
namespace {

// 离屏画一帧纯色再读回像素，核对颜色与行序约定（RGBA8、左上原点）。
void check_offscreen_clear(GraphicsBackend backend) {
  DeviceCreateInfo info{};
  info.backend = backend;
  info.enable_validation = false;
  info.app_name = "tamias-offscreen-test";
  Result<std::unique_ptr<RHIDevice>> device = RHIDevice::create(info);
  if (!device.has_value()) {
    GTEST_SKIP() << "backend unavailable: " << device.error();
  }
  Result<std::unique_ptr<SwapChain>> target = (*device)->create_offscreen_swap_chain(4, 4);
  if (!target.has_value()) {
    GTEST_SKIP() << "offscreen not supported: " << target.error();
  }
  EXPECT_TRUE((*target)->offscreen());
  ASSERT_EQ((*target)->width(), 4u);
  ASSERT_EQ((*target)->height(), 4u);

  Result<std::unique_ptr<CommandList>> commands = (*device)->create_command_list();
  ASSERT_TRUE(commands.has_value()) << commands.error();
  const float green[4] = {0.f, 1.f, 0.f, 1.f};
  ASSERT_TRUE((*device)->begin_frame(**target).has_value());
  (*commands)->begin();
  (*commands)->begin_render_pass(**target, green, 1.f);
  (*commands)->end_render_pass();
  (*commands)->end();
  ASSERT_TRUE((*device)->execute(**commands).has_value());
  ASSERT_TRUE((*device)->end_frame(**target).has_value());

  std::vector<std::uint8_t> pixels;
  ASSERT_TRUE((*target)->read_back_rgba(pixels).has_value());
  ASSERT_EQ(pixels.size(), 4u * 4u * 4u);
  for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
    EXPECT_LT(pixels[i + 0], 40) << "red channel at pixel " << i / 4;
    EXPECT_GT(pixels[i + 1], 200) << "green channel at pixel " << i / 4;
    EXPECT_LT(pixels[i + 2], 40) << "blue channel at pixel " << i / 4;
    EXPECT_EQ(pixels[i + 3], 255) << "alpha at pixel " << i / 4;
  }

  // 尺寸变了要能重建。
  ASSERT_TRUE((*target)->resize(8, 2).has_value());
  EXPECT_EQ((*target)->width(), 8u);
  EXPECT_EQ((*target)->height(), 2u);
}

TEST(OffscreenRender, VulkanClearsAndReadsBack) {
#if !defined(TAMIAS_HAS_RHI_VULKAN)
  GTEST_SKIP() << "Vulkan backend not built";
#else
  register_vulkan_backend();
  check_offscreen_clear(GraphicsBackend::Vulkan);
#endif
}

TEST(OffscreenRender, OpenGLClearsAndReadsBack) {
#if !defined(TAMIAS_HAS_RHI_OPENGL)
  GTEST_SKIP() << "OpenGL backend not built";
#else
  register_opengl_backend();
  check_offscreen_clear(GraphicsBackend::OpenGL);
#endif
}

}  // namespace
}  // namespace tamias
