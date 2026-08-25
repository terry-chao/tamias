#include "bim/line_location.h"
#include "bim/point_location.h"
#include "bim/surface_location.h"
#include "engine/document/document.h"
#include "engine/document/document_io.h"
#include "entity/column_entity.h"
#include "entity/slab_entity.h"
#include "entity/wall_entity.h"

#include <gtest/gtest.h>

namespace tamias {
namespace {

const Feature* feature_of(const Entity& entity, FeatureKind kind) {
  for (const Feature& feature : entity.model.features()) {
    if (feature.kind == kind) {
      return &feature;
    }
  }
  return nullptr;
}

TEST(Location, LineControlsWallLengthAndTransform) {
  WallEntity wall({0.f, 3.f, 0.f}, {0.f, 3.f, 4.f}, 0.2, 3.0);
  ASSERT_NE(wall.location, nullptr);
  ASSERT_EQ(wall.location->kind(), LocationKind::Line);
  auto* line = static_cast<LineLocation*>(wall.location.get());
  line->set_end({6.f, 0.f, 0.f});
  wall.sync_from_location(0.0);

  const Feature* profile = feature_of(wall, FeatureKind::RectProfile);
  ASSERT_NE(profile, nullptr);
  EXPECT_NEAR(wall.model.param(profile->id, "height", 0.0), 6.0, 1e-6);
  EXPECT_NEAR(wall.local_transform(0, 3), 3.f, 1e-5f);
  EXPECT_NEAR(wall.local_transform(1, 3), 3.f, 1e-5f);
}

TEST(Location, SlabUsesStoreyRelativeElevation) {
  Document document;
  const std::uint64_t storey_id = document.add_storey("2F", 3.0).id;
  document.set_active_storey(storey_id);

  SlabEntity slab({0.f, 3.f, 0.f}, 4.0, 3.0, 0.2);
  document.assign_active_storey(slab);
  auto mesh = slab.createGeom();
  ASSERT_TRUE(mesh);
  Entity* added =
      document.add_entity(std::make_unique<SlabEntity>(std::move(slab)), std::move(*mesh));
  ASSERT_NE(added, nullptr);
  ASSERT_NE(added->location, nullptr);
  EXPECT_EQ(added->location->storey_id(), storey_id);
  EXPECT_NEAR(added->location->elevation_offset(), 0.0, 1e-6);

  added->location->set_elevation_offset(1.25);
  ASSERT_TRUE(document.sync_entity_location(added->id));
  EXPECT_NEAR(added->local_transform(1, 3), 4.25f, 1e-5f);
  ASSERT_NE(document.scene().find(added->id), nullptr);
  EXPECT_EQ(document.scene().find(added->id)->parent, storey_id);
}

TEST(Location, StoreyAndPointRoundTrip) {
  Document document("location-roundtrip");
  const std::uint64_t storey_id = document.add_storey("1F", 1.5).id;
  document.set_active_storey(storey_id);

  ColumnEntity column({2.f, 2.f, 5.f});
  document.assign_active_storey(column);
  auto mesh = column.createGeom();
  ASSERT_TRUE(mesh);
  Entity* added =
      document.add_entity(std::make_unique<ColumnEntity>(std::move(column)), std::move(*mesh));
  ASSERT_NE(added, nullptr);
  const std::uint64_t entity_id = added->id;

  auto bytes = serialize_document(document);
  ASSERT_TRUE(bytes);
  auto loaded = deserialize_document(*bytes);
  ASSERT_TRUE(loaded);
  ASSERT_NE(loaded->bim().find_storey(storey_id), nullptr);
  EXPECT_EQ(loaded->bim().active_storey_id(), storey_id);
  const Entity* restored = loaded->entity(entity_id);
  ASSERT_NE(restored, nullptr);
  ASSERT_NE(restored->location, nullptr);
  EXPECT_EQ(restored->location->kind(), LocationKind::Point);
  EXPECT_EQ(restored->location->storey_id(), storey_id);
  EXPECT_NEAR(restored->location->elevation_offset(), 0.5, 1e-6);
}

}  // namespace
}  // namespace tamias
