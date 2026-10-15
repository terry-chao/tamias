#include "engine/math/camera.h"
#include "engine/render/rhi/device.h"
#include "engine/render/runtime/render_runtime.h"

#if defined(TAMIAS_HAS_RHI_VULKAN)
#include "engine/render/rhi/vulkan/vulkan_backend.h"
#endif
#if defined(TAMIAS_HAS_RHI_OPENGL)
#include "engine/render/rhi/opengl/opengl_backend.h"
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

namespace tamias {
namespace {

// overlay 线条走的是屏幕空间像素线（shaders/line.vert.hlsl）。线宽这条性质只有像素
// 说了算，所以这里真机离屏渲一帧再读回来：请求 6 px 就得画出 6 px；相机拉远一倍，
// 线在屏幕上的长度减半，粗细不变。
//
// 每张图都和同距离下不带这条线的参照图逐像素比——天空背景随距离变，不能跨距离比。
constexpr std::uint32_t kWidth = 256;
constexpr std::uint32_t kHeight = 192;
// 逐像素红通道差超过它就算这条线画到了：背景是深色天空（红通道 ~30..90），
// 线条是冷灰蓝（红通道 ~190）。
constexpr int kLitDiff = 40;
// 抗锯齿的判据：45 度线不可能对齐像素，边缘必然出现半覆盖的像素。
constexpr float kPartialHigh = 0.8f;

std::size_t red_index(std::uint32_t x, std::uint32_t y) {
  return (static_cast<std::size_t>(y) * kWidth + x) * 4;
}

FrameSubmission make_frame(const TurntableCamera& camera) {
  FrameSubmission frame{};
  frame.width = kWidth;
  frame.height = kHeight;
  frame.view = camera.view_matrix();
  frame.proj = camera.proj_matrix(static_cast<float>(kWidth) / static_cast<float>(kHeight));
  frame.eye_position = camera.eye_position();
  frame.view_distance = camera.distance();
  frame.fovy = camera.fovy();
  frame.orthographic = camera.orthographic();
  frame.mode = RenderMode::Shaded;
  frame.show_axes = false;  // 只量自己放进去的那条线
  return frame;
}

std::vector<std::uint8_t> draw_and_read(RenderThread& thread, std::uint64_t channel,
                                        FrameSubmission frame) {
  thread.submit_frame(channel, std::move(frame));
  std::vector<std::uint8_t> pixels;
  if (auto read = thread.read_channel_pixels(channel, pixels); !read) {
    pixels.clear();
  }
  return pixels;
}

int diff_at(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b,
            std::uint32_t x, std::uint32_t y) {
  return std::abs(static_cast<int>(a[red_index(x, y)]) -
                  static_cast<int>(b[red_index(x, y)]));
}

// rows = 有多少行画到了（线的粗细，像素）；cols = 有多少列画到了（线在屏幕上的长度）。
struct Extent {
  int rows = 0;
  int cols = 0;
};

Extent measure(const std::vector<std::uint8_t>& reference,
               const std::vector<std::uint8_t>& rendered) {
  Extent extent{};
  for (std::uint32_t y = 0; y < kHeight; ++y) {
    for (std::uint32_t x = 0; x < kWidth; ++x) {
      if (diff_at(reference, rendered, x, y) > kLitDiff) {
        ++extent.rows;
        break;
      }
    }
  }
  for (std::uint32_t x = 0; x < kWidth; ++x) {
    for (std::uint32_t y = 0; y < kHeight; ++y) {
      if (diff_at(reference, rendered, x, y) > kLitDiff) {
        ++extent.cols;
        break;
      }
    }
  }
  return extent;
}

void check_pixel_line_width(GraphicsBackend backend) {
  RenderDeviceConfig config{};
  config.backend = backend;
  config.enable_validation = false;
  std::shared_ptr<RenderThread> thread = RenderThreadPool::instance().acquire(config);
  if (thread == nullptr) {
    GTEST_SKIP() << "no usable render thread";
  }
  if (auto started = thread->start(); !started) {
    GTEST_SKIP() << "backend unavailable: " << started.error();
  }
  const std::uint64_t channel = thread->create_channel();
  thread->resize_offscreen_surface(channel, kWidth, kHeight);

  // 正交 + 正视图：世界 +X 朝屏幕右、+Y 朝屏幕上，世界到像素是干净的仿射映射
  // （1 m = height / (2 * distance * tan(fovy/2)) 像素），量线宽不掺透视缩放。
  TurntableCamera camera;
  camera.set_orthographic(true);
  camera.look_front();

  auto reference_at = [&](float distance) {
    TurntableCamera cam = camera;
    cam.set_distance(distance);
    return draw_and_read(*thread, channel, make_frame(cam));
  };
  // 一条悬空的水平线（y = 0.8，避开地面网格与世界 X 轴），世界长度 2 m。
  auto horizontal_at = [&](float distance, float width_px) {
    TurntableCamera cam = camera;
    cam.set_distance(distance);
    FrameSubmission frame = make_frame(cam);
    frame.grid_line_width_px = width_px;
    frame.grid_line_segments = {{-1.f, 0.8f, 0.f}, {1.f, 0.8f, 0.f}};
    return draw_and_read(*thread, channel, std::move(frame));
  };

  const std::vector<std::uint8_t> reference_near = reference_at(6.f);
  const std::vector<std::uint8_t> reference_far = reference_at(12.f);
  if (reference_near.size() != static_cast<std::size_t>(kWidth) * kHeight * 4 ||
      reference_far.size() != reference_near.size()) {
    GTEST_SKIP() << "offscreen readback unavailable";
  }

  const Extent thin_near = measure(reference_near, horizontal_at(6.f, 1.f));
  const Extent thin_far = measure(reference_far, horizontal_at(12.f, 1.f));
  const Extent thick_near = measure(reference_near, horizontal_at(6.f, 6.f));

  // 真的画出来了。
  EXPECT_GT(thin_near.rows, 0);
  EXPECT_GT(thin_near.cols, 0);

  // 世界尺度随距离缩放，线宽不缩放——这正是屏幕空间像素线的定义。
  EXPECT_LT(thin_far.cols, static_cast<int>(thin_near.cols * 0.75f))
      << "投影确实随距离缩小了（长度 " << thin_near.cols << " px -> " << thin_far.cols
      << " px）";
  EXPECT_LE(std::abs(thin_far.rows - thin_near.rows), 1)
      << "线宽随距离变了（" << thin_near.rows << " -> " << thin_far.rows << " px）";

  // 请求多少像素就画多少像素：1 px 线可能被劈成两行、6 px 线的边缘可能各占半行，
  // 所以只断言"明显更宽"加一个上界。
  EXPECT_GE(thick_near.rows, thin_near.rows + 4)
      << "线宽没跟着 grid_line_width_px 走（1 px -> " << thin_near.rows << " 行，6 px -> "
      << thick_near.rows << " 行）";
  EXPECT_LE(thick_near.rows, 8) << "6 px 的线不该画成 " << thick_near.rows << " 行";

  // 抗锯齿：45 度线的像素中心到中线不可能都对得整整齐齐，边缘一定有半覆盖像素。
  // 用调试段的黄线而不是预览线的青色：黄线的红分量满值，压过天空最亮的那道渐变，
  // 判据（红通道差）才不会因为背景变亮而失效。
  {
    TurntableCamera cam = camera;
    cam.set_distance(6.f);
    FrameSubmission frame = make_frame(cam);
    frame.line_width_px = 3.f;
    frame.debug_line_segments = {{-1.f, -0.8f, 0.f}, {1.f, 1.2f, 0.f}};
    const std::vector<std::uint8_t> rendered =
        draw_and_read(*thread, channel, std::move(frame));
    ASSERT_EQ(rendered.size(), reference_near.size());
    int max_diff = 0;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
      for (std::uint32_t x = 0; x < kWidth; ++x) {
        max_diff = (std::max)(max_diff, diff_at(reference_near, rendered, x, y));
      }
    }
    EXPECT_GT(max_diff, kLitDiff) << "45 度线没画出来";
    int partial = 0;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
      for (std::uint32_t x = 0; x < kWidth; ++x) {
        const int diff = diff_at(reference_near, rendered, x, y);
        if (diff > kLitDiff && diff < static_cast<int>(max_diff * kPartialHigh)) {
          ++partial;
        }
      }
    }
    EXPECT_GT(partial, 5) << "边缘没有半覆盖像素，说明是硬边线、没有抗锯齿";
  }

  // 世界坐标轴现在也走这条通路（以前是一条独立的 LineList 网格 + 单独 draw）。
  {
    TurntableCamera cam = camera;
    cam.set_distance(6.f);
    FrameSubmission frame = make_frame(cam);
    frame.show_axes = true;
    const std::vector<std::uint8_t> rendered =
        draw_and_read(*thread, channel, std::move(frame));
    ASSERT_EQ(rendered.size(), reference_near.size());
    EXPECT_GT(measure(reference_near, rendered).rows, 0) << "世界坐标轴应该被画出来";
  }

  // 合批：200 根轴线一次画完。以前是每段一次 push constant + draw（轴网一密就上千次）。
  {
    TurntableCamera cam = camera;
    cam.set_distance(6.f);
    FrameSubmission frame = make_frame(cam);
    frame.grid_line_segments.reserve(400);
    for (int i = 0; i < 200; ++i) {
      const float z = -2.f + 0.02f * static_cast<float>(i);
      frame.grid_line_segments.push_back({-1.f, 0.8f, z});
      frame.grid_line_segments.push_back({1.f, 0.8f, z});
    }
    const std::vector<std::uint8_t> rendered =
        draw_and_read(*thread, channel, std::move(frame));
    ASSERT_EQ(rendered.size(), reference_near.size());
    const RenderFrameStats stats = thread->last_stats();
    EXPECT_EQ(stats.overlay_line_instances, 200u);
    EXPECT_EQ(stats.overlay_line_draws, 1u)
        << "一批线段应该一次实例化 draw 画完，而不是逐段提交";
  }

  thread->destroy_channel(channel);
  RenderThreadPool::instance().shutdown();
}

TEST(OverlayLineRender, VulkanDrawsScreenSpacePixelLines) {
#if !defined(TAMIAS_HAS_RHI_VULKAN)
  GTEST_SKIP() << "Vulkan backend not built";
#else
  register_vulkan_backend();  // 测试二进制不会自动登记后端（app 才登记）
  check_pixel_line_width(GraphicsBackend::Vulkan);
#endif
}

TEST(OverlayLineRender, OpenGlDrawsScreenSpacePixelLines) {
#if !defined(TAMIAS_HAS_RHI_OPENGL)
  GTEST_SKIP() << "OpenGL backend not built";
#else
  register_opengl_backend();
  check_pixel_line_width(GraphicsBackend::OpenGL);
#endif
}

}  // namespace
}  // namespace tamias
