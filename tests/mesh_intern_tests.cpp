#include "command/set_feature_param_command.h"
#include "engine/document/document.h"
#include "engine/io/mesh_io.h"
#include "engine/modeling/feature.h"
#include "entity/box_entity.h"
#include "entity/column_entity.h"

#include <gtest/gtest.h>

namespace tamias {
namespace {

TEST(MeshIntern, IdenticalColumnsShareOneAsset) {
  Document doc;
  std::uint64_t shared = 0;
  for (int i = 0; i < 8; ++i) {
    ColumnEntity column({static_cast<float>(i), 0.f, 0.f}, 0.4, 0.4, 3.0);
    auto mesh = column.createGeom();
    ASSERT_TRUE(mesh) << mesh.error();
    Entity* added =
        doc.add_entity(std::make_unique<ColumnEntity>(std::move(column)), std::move(*mesh));
    ASSERT_NE(added, nullptr);
    if (i == 0) {
      shared = added->mesh_asset_id;
    } else {
      EXPECT_EQ(added->mesh_asset_id, shared);
    }
  }
  EXPECT_EQ(doc.entities().size(), 8u);
  EXPECT_EQ(doc.meshes().size(), 1u);
  EXPECT_EQ(doc.scene().nodes().size(), 8u);
}

TEST(MeshIntern, ImportIdenticalCubesShareOneAsset) {
  Document doc;
  const std::uint64_t a =
      doc.add_import_mesh("front", make_demo_cube(), Mat4::identity(), {1.f, 1.f, 1.f});
  const std::uint64_t b =
      doc.add_import_mesh("behind", make_demo_cube(), translate({0.f, 0.f, 40.f}), {1.f, 1.f, 1.f});
  EXPECT_EQ(a, b);
  EXPECT_EQ(doc.meshes().size(), 1u);
  EXPECT_EQ(doc.scene().nodes().size(), 2u);
}

TEST(MeshIntern, ParamChangeCopyOnWrite) {
  Document doc;
  auto add_box = [&](Vec3 origin) {
    BoxEntity box(origin);
    auto mesh = box.createGeom();
    EXPECT_TRUE(mesh);
    return doc.add_entity(std::make_unique<BoxEntity>(std::move(box)), std::move(*mesh));
  };
  Entity* a = add_box({0.f, 0.f, 0.f});
  Entity* b = add_box({3.f, 0.f, 0.f});
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(a->mesh_asset_id, b->mesh_asset_id);
  EXPECT_EQ(doc.meshes().size(), 1u);

  const Feature* profile = nullptr;
  for (const Feature& f : a->model.features()) {
    if (f.kind == FeatureKind::RectProfile) {
      profile = &f;
      break;
    }
  }
  ASSERT_NE(profile, nullptr);
  SetFeatureParamCommand cmd(doc, a->id, profile->id, "width", 2.0);
  const auto changed = cmd.execute();
  ASSERT_TRUE(changed) << changed.error();
  EXPECT_NE(a->mesh_asset_id, b->mesh_asset_id);
  EXPECT_EQ(doc.meshes().size(), 2u);
}

TEST(MeshIntern, DeleteSharedKeepsMeshUntilLastRef) {
  Document doc;
  auto add_col = [&](float x) {
    ColumnEntity column({x, 0.f, 0.f}, 0.4, 0.4, 3.0);
    auto mesh = column.createGeom();
    EXPECT_TRUE(mesh);
    return doc.add_entity(std::make_unique<ColumnEntity>(std::move(column)), std::move(*mesh));
  };
  Entity* a = add_col(0.f);
  Entity* b = add_col(2.f);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  const std::uint64_t mesh_id = a->mesh_asset_id;
  const std::uint64_t keep = b->id;
  doc.remove_entity(a->id);
  EXPECT_EQ(doc.meshes().size(), 1u);
  EXPECT_NE(doc.mesh(mesh_id), nullptr);
  EXPECT_EQ(doc.entity(keep)->mesh_asset_id, mesh_id);
  doc.remove_entity(keep);
  EXPECT_TRUE(doc.meshes().empty());
}

}  // namespace
}  // namespace tamias
