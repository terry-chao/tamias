#include "io/mesh_io.h"
#include "math/math.h"
#include "view/picking.h"
#include "modeling/occt_shape_ops.h"
#include "modeling/shape_ops.h"
#include "engine/render/render_runtime.h"

#include <gtest/gtest.h>

using namespace tamias;

TEST(Math, AabbExpand) {
  Aabb box{};
  box.expand({1, 2, 3});
  box.expand({-1, 0, 4});
  EXPECT_FLOAT_EQ(box.min.x, -1.f);
  EXPECT_FLOAT_EQ(box.max.z, 4.f);
}

TEST(MeshIo, DemoCubeHasTriangles) {
  const auto mesh = make_demo_cube();
  EXPECT_FALSE(mesh.indices.empty());
  EXPECT_TRUE(mesh.bounds.valid());
}

TEST(Picking, RayHitsCube) {
  Document doc("t");
  MeshAsset asset{};
  asset.cpu = make_demo_cube();
  auto& stored = doc.add_mesh(std::move(asset));
  SceneNode node{};
  node.mesh_asset_id = stored.id;
  node.world_bounds = stored.cpu.bounds;
  doc.scene().add_node(std::move(node));

  Bvh bvh;
  bvh.build(doc);
  Ray ray{{0.f, 0.f, 5.f}, {0.f, 0.f, -1.f}};
  auto hit = bvh.closest_hit(ray, doc);
  ASSERT_TRUE(hit.has_value());
}

TEST(Modeling, MeshShapeTessellate) {
  MeshShape shape(make_demo_cube());
  auto mesh = shape.tessellate(0.1);
  ASSERT_TRUE(mesh.has_value());
  EXPECT_EQ(shape.backend_name(), "mesh");
}

TEST(RenderConfig, OpenGlDoesNotShare) {
  RenderDeviceConfig a{GraphicsBackend::OpenGL, true};
  RenderDeviceConfig b{GraphicsBackend::OpenGL, true};
  EXPECT_FALSE(a.shares_execution_thread_with(b));
  RenderDeviceConfig v0{GraphicsBackend::Vulkan, true};
  RenderDeviceConfig v1{GraphicsBackend::Vulkan, true};
  EXPECT_TRUE(v0.shares_execution_thread_with(v1));
}

#if defined(TAMIAS_HAS_OCCT)
TEST(Occt, TessellateBox) {
  auto mesh = tessellate_occt_box_for_tests();
  ASSERT_TRUE(mesh.has_value()) << mesh.error();
  EXPECT_FALSE(mesh->indices.empty());
  EXPECT_TRUE(mesh->bounds.valid());
}
#endif
