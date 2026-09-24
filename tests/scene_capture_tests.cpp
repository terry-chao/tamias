#include "engine/document/document.h"
#include "engine/document/scene_capture.h"
#include "engine/graphics/mesh.h"
#include "engine/render/runtime/render_runtime.h"

#if defined(TAMIAS_HAS_RHI_VULKAN)
#include "engine/render/rhi/vulkan/vulkan_backend.h"
#endif

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace tamias {
namespace {

// 一块躺在 XZ 平面上的 2×2 方板：够让默认相机看得见。
MeshCpu make_floor_quad() {
  MeshCpu mesh;
  const Vec3 corners[4] = {{-1.f, 0.f, -1.f}, {1.f, 0.f, -1.f}, {1.f, 0.f, 1.f}, {-1.f, 0.f, 1.f}};
  for (const Vec3& corner : corners) {
    Vertex vertex{};
    vertex.position = corner;
    vertex.normal = {0.f, 1.f, 0.f};
    vertex.uv = {0.f, 0.f};
    mesh.vertices.push_back(vertex);
    mesh.bounds.expand(corner);
  }
  mesh.indices = {0, 1, 2, 0, 2, 3};
  return mesh;
}

TEST(SceneCapture, RendersGeometryOffscreenWithoutWindow) {
#if !defined(TAMIAS_HAS_RHI_VULKAN)
  GTEST_SKIP() << "Vulkan backend not built";
#else
  register_vulkan_backend();  // 测试二进制不会自动登记后端（app 才登记）
  RenderDeviceConfig config{};
  config.enable_validation = false;
  std::shared_ptr<RenderThread> thread = RenderThreadPool::instance().acquire(config);
  if (thread == nullptr) {
    GTEST_SKIP() << "no usable render thread (no GPU / no runtime?)";
  }
  if (auto started = thread->start(); !started) {
    GTEST_SKIP() << "backend unavailable: " << started.error();
  }

  SceneCaptureRequest request{};
  request.width = 64;
  request.height = 48;
  request.distance = 6.f;
  request.yaw = 0.785398163f;
  request.pitch = 0.5f;

  // 先渲一张空场景做对照。
  Document empty_document("capture-empty");
  Result<std::vector<std::uint8_t>> empty = capture_document_rgba(*thread, empty_document, request);
  if (!empty.has_value()) {
    GTEST_SKIP() << "offscreen capture unavailable: " << empty.error();
  }
  ASSERT_EQ(empty->size(), 64u * 48u * 4u);

  // 加一块板再渲：两张图必须不同——说明几何真的被画进去了，而不是每次都返回同一张底图。
  Document document("capture-quad");
  document.add_import_mesh("quad", make_floor_quad(), Mat4::identity(), Vec3{0.9f, 0.3f, 0.2f});
  Result<std::vector<std::uint8_t>> rendered = capture_document_rgba(*thread, document, request);
  ASSERT_TRUE(rendered.has_value()) << rendered.error();
  ASSERT_EQ(rendered->size(), 64u * 48u * 4u);
  EXPECT_NE(*empty, *rendered);

  // 尺寸也要能变（导出时经常要指定分辨率）。
  request.width = 32;
  request.height = 32;
  Result<std::vector<std::uint8_t>> small = capture_document_rgba(*thread, document, request);
  ASSERT_TRUE(small.has_value()) << small.error();
  EXPECT_EQ(small->size(), 32u * 32u * 4u);

  RenderThreadPool::instance().shutdown();
#endif
}

}  // namespace
}  // namespace tamias
