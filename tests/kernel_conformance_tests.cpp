#include "engine/graphics/mesh.h"
#include "engine/modeling/evaluator.h"
#include "engine/modeling/feature.h"
#include "engine/modeling/kernel/kernel.h"
#include "engine/modeling/linked_kernels.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tamias {
namespace {

// 跨后端的验收网：同一批场景对**每一个注册进来的内核**跑一遍。
// 只用 ModelKernel 接口，不碰任何 OCCT 类型——接口里一旦混进某个后端的语义，这里就会红。
// 后端声明不支持的动词就跳过（capabilities().supports），不是失败。
void for_each_kernel(const std::function<void(ModelKernel&, const KernelCapabilities&)>& body) {
  register_linked_kernels();
  const std::vector<KernelBackend> backends = registered_kernel_backends();
  ASSERT_FALSE(backends.empty()) << "没有注册任何内核后端";
  for (KernelBackend backend : backends) {
    auto kernel = ModelKernel::create(KernelCreateInfo{backend});
    ASSERT_TRUE(kernel) << kernel.error();
    SCOPED_TRACE(std::string("backend=") + to_string(backend));
    body(**kernel, (*kernel)->capabilities());
  }
}

constexpr double kNearly = 1e-6;

double span(double a, double b) { return std::fabs(b - a); }

}  // namespace

TEST(KernelConformance, RectFaceExtrudesToBox) {
  for_each_kernel([](ModelKernel& kernel, const KernelCapabilities& caps) {
    if (!caps.supports(KernelVerb::RectFace) || !caps.supports(KernelVerb::Extrude) ||
        !caps.supports(KernelVerb::Tessellate)) {
      GTEST_SKIP() << "backend does not implement the rect/extrude/tessellate path";
    }
    auto face = kernel.make_rect_face(2.0, 1.0);
    ASSERT_TRUE(face) << face.error();
    auto solid = kernel.extrude(**face, 3.0);
    ASSERT_TRUE(solid) << solid.error();
    if (caps.supports(KernelVerb::Bounds)) {
      auto bounds = kernel.bounds(**solid);
      ASSERT_TRUE(bounds) << bounds.error();
      ASSERT_TRUE(bounds->valid());
      EXPECT_NEAR(span(bounds->min.x, bounds->max.x), 2.0, 1e-4);
      EXPECT_NEAR(span(bounds->min.y, bounds->max.y), 3.0, 1e-4);
      EXPECT_NEAR(span(bounds->min.z, bounds->max.z), 1.0, 1e-4);
    }
    auto mesh = kernel.tessellate(**solid, 0.05);
    ASSERT_TRUE(mesh) << mesh.error();
    EXPECT_FALSE(mesh->indices.empty());
  });
}

TEST(KernelConformance, CircleFaceExtrudesRound) {
  for_each_kernel([](ModelKernel& kernel, const KernelCapabilities& caps) {
    if (!caps.supports(KernelVerb::CircleFace) || !caps.supports(KernelVerb::Extrude) ||
        !caps.supports(KernelVerb::Tessellate)) {
      GTEST_SKIP() << "backend does not implement the circle/extrude/tessellate path";
    }
    auto face = kernel.make_circle_face(0.5);
    ASSERT_TRUE(face) << face.error();
    auto solid = kernel.extrude(**face, 1.0);
    ASSERT_TRUE(solid) << solid.error();
    if (caps.supports(KernelVerb::Bounds)) {
      auto bounds = kernel.bounds(**solid);
      ASSERT_TRUE(bounds) << bounds.error();
      EXPECT_NEAR(span(bounds->min.x, bounds->max.x), 1.0, 1e-3);
      EXPECT_NEAR(span(bounds->min.y, bounds->max.y), 1.0, 1e-4);
      EXPECT_NEAR(span(bounds->min.z, bounds->max.z), 1.0, 1e-3);
    }
    auto mesh = kernel.tessellate(**solid, 0.05);
    ASSERT_TRUE(mesh) << mesh.error();
    EXPECT_FALSE(mesh->indices.empty());
  });
}

TEST(KernelConformance, PolygonFaceExtrudes) {
  for_each_kernel([](ModelKernel& kernel, const KernelCapabilities& caps) {
    if (!caps.supports(KernelVerb::PolygonFace) || !caps.supports(KernelVerb::Extrude)) {
      GTEST_SKIP() << "backend does not implement the polygon path";
    }
    const Vec3 loop[4] = {
        {-1.f, 0.f, -1.f}, {1.f, 0.f, -1.f}, {1.f, 0.f, 1.f}, {-1.f, 0.f, 1.f}};
    auto face = kernel.make_polygon_face(loop);
    ASSERT_TRUE(face) << face.error();
    auto solid = kernel.extrude(**face, 2.0);
    ASSERT_TRUE(solid) << solid.error();
    auto mesh = kernel.tessellate(**solid, 0.05);
    ASSERT_TRUE(mesh) << mesh.error();
    EXPECT_FALSE(mesh->indices.empty());
  });
}

TEST(KernelConformance, BooleanCutRemovesMaterial) {
  for_each_kernel([](ModelKernel& kernel, const KernelCapabilities& caps) {
    if (!caps.supports(KernelVerb::Boolean)) {
      GTEST_SKIP() << "backend does not implement booleans";
    }
    auto base_face = kernel.make_rect_face(2.0, 2.0);
    ASSERT_TRUE(base_face) << base_face.error();
    auto base = kernel.extrude(**base_face, 2.0);
    ASSERT_TRUE(base) << base.error();
    auto tool_face = kernel.make_rect_face(1.0, 1.0);
    ASSERT_TRUE(tool_face) << tool_face.error();
    auto tool = kernel.extrude(**tool_face, 4.0);
    ASSERT_TRUE(tool) << tool.error();
    // 工具体下移一点，让它穿透母体而不是底面共面——共面是布尔算法的退化情形，
    // 换哪个内核都容易失败，测试里避开它。
    if (caps.supports(KernelVerb::Transform)) {
      auto moved = kernel.transform(**tool, Vec3{0.f, -1.f, 0.f});
      ASSERT_TRUE(moved) << moved.error();
      tool = std::move(*moved);
    }

    auto cut = kernel.boolean(**base, **tool, BooleanOp::Cut);
    ASSERT_TRUE(cut) << cut.error();
    auto mesh = kernel.tessellate(**cut, 0.05);
    ASSERT_TRUE(mesh) << mesh.error();
    EXPECT_FALSE(mesh->indices.empty());
    // 挖掉中间的方柱后，剩下的是一个环：面数应多于原来的盒子。
    auto base_mesh = kernel.tessellate(**base, 0.05);
    ASSERT_TRUE(base_mesh) << base_mesh.error();
    EXPECT_GT(mesh->indices.size(), base_mesh->indices.size());
  });
}

TEST(KernelConformance, TransformMovesTheBody) {
  for_each_kernel([](ModelKernel& kernel, const KernelCapabilities& caps) {
    if (!caps.supports(KernelVerb::Transform) || !caps.supports(KernelVerb::Bounds)) {
      GTEST_SKIP() << "backend does not implement transform/bounds";
    }
    auto face = kernel.make_rect_face(1.0, 1.0);
    ASSERT_TRUE(face) << face.error();
    auto solid = kernel.extrude(**face, 1.0);
    ASSERT_TRUE(solid) << solid.error();
    auto before = kernel.bounds(**solid);
    ASSERT_TRUE(before) << before.error();
    auto moved = kernel.transform(**solid, Vec3{0.f, 5.f, 0.f});
    ASSERT_TRUE(moved) << moved.error();
    auto after = kernel.bounds(**moved);
    ASSERT_TRUE(after) << after.error();
    EXPECT_NEAR(after->min.y - before->min.y, 5.0, 1e-4);
    EXPECT_NEAR(after->min.x, before->min.x, 1e-4);
  });
}

TEST(KernelConformance, CylinderVerbMakesARoundBar) {
  for_each_kernel([](ModelKernel& kernel, const KernelCapabilities& caps) {
    if (!caps.supports(KernelVerb::Cylinder) || !caps.supports(KernelVerb::Tessellate)) {
      GTEST_SKIP() << "backend does not implement the cylinder verb";
    }
    // 沿 +Y：半径 0.5、高 2、中心在原点 → 包围盒 1 × 2 × 1。
    auto up = kernel.cylinder(0.5, 2.0, Vec3{0.f, 0.f, 0.f}, Vec3{0.f, 1.f, 0.f});
    ASSERT_TRUE(up) << up.error();
    if (caps.supports(KernelVerb::Bounds)) {
      auto bounds = kernel.bounds(**up);
      ASSERT_TRUE(bounds) << bounds.error();
      EXPECT_NEAR(span(bounds->min.x, bounds->max.x), 1.0, 1e-3);
      EXPECT_NEAR(span(bounds->min.y, bounds->max.y), 2.0, 1e-3);
      EXPECT_NEAR(span(bounds->min.z, bounds->max.z), 1.0, 1e-3);
    }
    auto mesh = kernel.tessellate(**up, 0.05);
    ASSERT_TRUE(mesh) << mesh.error();
    EXPECT_FALSE(mesh->indices.empty());

    // 换一条轴（+X）：长边应该跟着转到 X 上。
    auto along_x = kernel.cylinder(0.5, 2.0, Vec3{0.f, 0.f, 0.f}, Vec3{1.f, 0.f, 0.f});
    ASSERT_TRUE(along_x) << along_x.error();
    if (caps.supports(KernelVerb::Bounds)) {
      auto bounds = kernel.bounds(**along_x);
      ASSERT_TRUE(bounds) << bounds.error();
      EXPECT_NEAR(span(bounds->min.x, bounds->max.x), 2.0, 1e-3);
      EXPECT_NEAR(span(bounds->min.y, bounds->max.y), 1.0, 1e-3);
      EXPECT_NEAR(span(bounds->min.z, bounds->max.z), 1.0, 1e-3);
    }
  });
}

TEST(KernelConformance, EdgesAndMeasuresAgree) {
  for_each_kernel([](ModelKernel& kernel, const KernelCapabilities& caps) {
    if (!caps.supports(KernelVerb::Edges) || !caps.supports(KernelVerb::MeasureEdges)) {
      GTEST_SKIP() << "backend does not expose edge measurement";
    }
    auto face = kernel.make_rect_face(2.0, 1.0);
    ASSERT_TRUE(face) << face.error();
    auto solid = kernel.extrude(**face, 3.0);
    ASSERT_TRUE(solid) << solid.error();

    auto edges = kernel.edges(**solid);
    ASSERT_TRUE(edges) << edges.error();
    EXPECT_FALSE(edges->empty());
    auto measures = kernel.measure_edges(**solid);
    ASSERT_TRUE(measures) << measures.error();
    ASSERT_EQ(measures->size(), edges->size());

    auto bounds = kernel.bounds(**solid);
    ASSERT_TRUE(bounds) << bounds.error();
    // 测量要在体的包围盒附近（留一点容差，闭合边的质心允许在内部）。
    const Aabb grow{{bounds->min.x - 1e-3f, bounds->min.y - 1e-3f, bounds->min.z - 1e-3f},
                    {bounds->max.x + 1e-3f, bounds->max.y + 1e-3f, bounds->max.z + 1e-3f}};
    for (std::size_t i = 0; i < measures->size(); ++i) {
      const EdgeMeasure& m = (*measures)[i];
      EXPECT_GT(m.length, 0.0) << "edge " << i;
      EXPECT_GE(m.mid.x, grow.min.x) << "edge " << i;
      EXPECT_LE(m.mid.x, grow.max.x) << "edge " << i;
      EXPECT_GE(m.mid.y, grow.min.y) << "edge " << i;
      EXPECT_LE(m.mid.y, grow.max.y) << "edge " << i;
      if (m.has_dir) {
        EXPECT_NEAR(std::sqrt(static_cast<double>(dot(m.dir, m.dir))), 1.0, 1e-3)
            << "edge " << i;
      }
      EXPECT_NE(m.key, 0u) << "edge " << i << " has no identity key";
    }
    // 同一条几何边被多个面共享时，重复枚举必须共享同一个 key。
    std::size_t with_shared_key = 0;
    for (std::size_t i = 0; i < measures->size(); ++i) {
      for (std::size_t j = i + 1; j < measures->size(); ++j) {
        if ((*measures)[i].key == (*measures)[j].key) {
          ++with_shared_key;
        }
      }
    }
    EXPECT_GT(with_shared_key, 0u) << "盒子的棱应由两个面共享，key 却没重合";
  });
}

TEST(KernelConformance, FilletAndChamferAreHonest) {
  for_each_kernel([](ModelKernel& kernel, const KernelCapabilities& caps) {
    if (!caps.supports(KernelVerb::Edges)) {
      GTEST_SKIP() << "backend does not expose edges";
    }
    auto face = kernel.make_rect_face(1.0, 1.0);
    ASSERT_TRUE(face) << face.error();
    auto solid = kernel.extrude(**face, 1.0);
    ASSERT_TRUE(solid) << solid.error();
    auto edges = kernel.edges(**solid);
    ASSERT_TRUE(edges) << edges.error();
    ASSERT_FALSE(edges->empty());
    const EdgeId ids[1] = {(*edges)[0]};

    // 声明支持就必须能做；声明不支持就必须干净报错（不能崩、不能假装成功）。
    auto filleted = kernel.fillet(**solid, ids, 0.05);
    if (caps.supports(KernelVerb::Fillet)) {
      ASSERT_TRUE(filleted) << filleted.error();
      auto mesh = kernel.tessellate(**filleted, 0.05);
      ASSERT_TRUE(mesh) << mesh.error();
      EXPECT_FALSE(mesh->indices.empty());
    } else {
      EXPECT_FALSE(filleted);
    }

    auto chamfered = kernel.chamfer(**solid, ids, 0.05);
    if (caps.supports(KernelVerb::Chamfer)) {
      ASSERT_TRUE(chamfered) << chamfered.error();
      auto mesh = kernel.tessellate(**chamfered, 0.05);
      ASSERT_TRUE(mesh) << mesh.error();
      EXPECT_FALSE(mesh->indices.empty());
    } else {
      EXPECT_FALSE(chamfered);
    }
  });
}

TEST(KernelConformance, EvaluatorRunsFeatureTreeOnEveryKernel) {
  for_each_kernel([](ModelKernel& kernel, const KernelCapabilities& caps) {
    if (!caps.supports(KernelVerb::RectFace) || !caps.supports(KernelVerb::Extrude)) {
      GTEST_SKIP() << "backend does not implement the rect/extrude path";
    }
    FeatureModel model;
    const std::uint64_t profile =
        model
            .add_feature(FeatureKind::RectProfile, {}, {{"width", 3.0}, {"height", 1.5}})
            .id;
    model.add_feature(FeatureKind::Extrude, {profile}, {{"depth", 2.0}});

    auto mesh = evaluate_feature_model(model, kernel, 0.05);
    ASSERT_TRUE(mesh) << mesh.error();
    EXPECT_FALSE(mesh->indices.empty());
    EXPECT_NEAR(span(mesh->bounds.min.x, mesh->bounds.max.x), 3.0, 1e-3);
    EXPECT_NEAR(span(mesh->bounds.min.y, mesh->bounds.max.y), 2.0, 1e-3);
  });
}

}  // namespace tamias
