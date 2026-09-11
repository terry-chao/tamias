#include "engine/render/mesh_lod.h"
#include "engine/document/document.h"
#include "engine/document/tess_cache.h"
#include "engine/render/resident_cache.h"
#include "engine/render/scene_graph.h"
#include "entity/box_entity.h"
#include "entity/column_entity.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace tamias {
namespace {

TEST(MeshLod, FromPixelsThresholds) {
  EXPECT_EQ(mesh_lod_from_pixels(0.5f), MeshLod::Box);
  EXPECT_EQ(mesh_lod_from_pixels(3.9f), MeshLod::Box);
  EXPECT_EQ(mesh_lod_from_pixels(4.f), MeshLod::Coarse);
  EXPECT_EQ(mesh_lod_from_pixels(79.f), MeshLod::Coarse);
  EXPECT_EQ(mesh_lod_from_pixels(80.f), MeshLod::Work);
}

TEST(MeshLod, HysteresisHoldsNearBoundary) {
  const MeshLod stay_work =
      select_mesh_lod(70.f, MeshLod::Work, false, false);
  EXPECT_EQ(stay_work, MeshLod::Work);

  const MeshLod drop_work =
      select_mesh_lod(kMeshLodCoarsePixels * kMeshLodDowngradeScale, MeshLod::Work, false,
                      false);
  EXPECT_NE(drop_work, MeshLod::Work);

  const MeshLod stay_coarse =
      select_mesh_lod(90.f, MeshLod::Coarse, false, false);
  EXPECT_EQ(stay_coarse, MeshLod::Coarse);

  const MeshLod upgrade =
      select_mesh_lod(kMeshLodCoarsePixels * kMeshLodUpgradeScale, MeshLod::Coarse, false,
                      false);
  EXPECT_EQ(upgrade, MeshLod::Work);
}

TEST(MeshLod, SelectedAndLinesForceWork) {
  EXPECT_EQ(select_mesh_lod(1.f, MeshLod::Box, true, false), MeshLod::Work);
  EXPECT_EQ(select_mesh_lod(1.f, MeshLod::Box, false, true), MeshLod::Work);
}

TEST(MeshLod, SkipSubpixel) {
  EXPECT_TRUE(mesh_lod_skip_draw(0.5f, false));
  EXPECT_FALSE(mesh_lod_skip_draw(0.5f, true));
  EXPECT_FALSE(mesh_lod_skip_draw(2.f, false));
}

TEST(MeshLod, ProjectedPixelsGrowWhenCloser) {
  Aabb box{};
  box.min = {-1.f, -1.f, -1.f};
  box.max = {1.f, 1.f, 1.f};
  const float far_px = projected_aabb_pixels(box, {0.f, 0.f, 200.f}, 0.8f, 720.f);
  const float near_px = projected_aabb_pixels(box, {0.f, 0.f, 8.f}, 0.8f, 720.f);
  EXPECT_GT(near_px, far_px);
}

TEST(TessCache, ReplaceEntityMeshInvalidatesOldLodsWhenUnreferenced) {
  Document doc;
  ColumnEntity column({0.f, 0.f, 0.f}, 0.4, 0.4, 3.0);
  auto mesh = column.createGeom();
  ASSERT_TRUE(mesh) << mesh.error();
  Entity* a = doc.add_entity(std::make_unique<ColumnEntity>(std::move(column)), std::move(*mesh));
  ASSERT_NE(a, nullptr);
  const std::uint64_t geom = a->mesh_asset_id;
  EXPECT_NE(doc.tess_cache().set_for(geom).coarse, 0u);

  BoxEntity box({5.f, 0.f, 0.f});
  auto box_mesh = box.createGeom();
  ASSERT_TRUE(box_mesh);
  ASSERT_TRUE(doc.replace_entity_mesh(a->id, std::move(*box_mesh)));
  EXPECT_NE(a->mesh_asset_id, geom);
  EXPECT_EQ(doc.tess_cache().set_for(geom).coarse, 0u);
  EXPECT_NE(doc.tess_cache().set_for(a->mesh_asset_id).coarse, 0u);
}

TEST(TessCache, SharedGeometryKeepsLodsWhenSiblingChanges) {
  Document doc;
  ColumnEntity ca({0.f, 0.f, 0.f}, 0.4, 0.4, 3.0);
  ColumnEntity cb({2.f, 0.f, 0.f}, 0.4, 0.4, 3.0);
  auto ma = ca.createGeom();
  auto mb = cb.createGeom();
  ASSERT_TRUE(ma);
  ASSERT_TRUE(mb);
  Entity* a = doc.add_entity(std::make_unique<ColumnEntity>(std::move(ca)), std::move(*ma));
  Entity* b = doc.add_entity(std::make_unique<ColumnEntity>(std::move(cb)), std::move(*mb));
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_EQ(a->mesh_asset_id, b->mesh_asset_id);
  const std::uint64_t shared = a->mesh_asset_id;
  const std::uint64_t coarse = doc.tess_cache().set_for(shared).coarse;
  ASSERT_NE(coarse, 0u);

  BoxEntity box({5.f, 0.f, 0.f});
  auto box_mesh = box.createGeom();
  ASSERT_TRUE(box_mesh);
  // Change only A by replacing with a unique box mesh.
  ASSERT_TRUE(doc.replace_entity_mesh(a->id, std::move(*box_mesh)));
  EXPECT_EQ(b->mesh_asset_id, shared);
  EXPECT_EQ(doc.tess_cache().set_for(shared).coarse, coarse);
}

TEST(ResidentCache, EvictsOldestWhenOverBudget) {
  ResidentCache cache(100);
  cache.insert(1, 10, 60);
  cache.insert(2, 20, 60);
  const std::vector<std::uint64_t> over = cache.over_budget_assets();
  ASSERT_EQ(over.size(), 1u);
  EXPECT_EQ(over[0], 1u);
  (void)cache.lookup(2);
  cache.insert(3, 30, 60);
  const std::vector<std::uint64_t> over2 = cache.over_budget_assets();
  ASSERT_FALSE(over2.empty());
  EXPECT_EQ(over2[0], 1u);
}

TEST(MeshLod, FeatureTessellationRecordsFaceRanges) {
  BoxEntity box({0.f, 0.f, 0.f});
  auto mesh = box.createGeom();
  if (!mesh) {
    GTEST_SKIP() << mesh.error();
  }
  EXPECT_FALSE(mesh->faces.empty());
  std::uint32_t covered = 0;
  for (const MeshFaceRange& face : mesh->faces) {
    EXPECT_GT(face.index_count, 0u);
    EXPECT_TRUE(face.bounds.valid());
    covered += face.index_count;
  }
  EXPECT_EQ(covered, static_cast<std::uint32_t>(mesh->indices.size()));
}

}  // namespace
}  // namespace tamias
