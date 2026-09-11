#include "entity/column_entity.h"

#include "bim/point_location.h"

namespace tamias {

ColumnEntity::ColumnEntity(Vec3 position, double width, double depth, double height)
    : FamilyEntity(EntityKind::Column, "Concrete Column") {
  name = "column";
  auto& profile =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", width}, {"height", depth}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  location = std::make_unique<PointLocation>(
      Vec3{position.x, 0.f, position.z}, 0, static_cast<double>(position.y));
  sync_from_location(0.0);
}

ColumnEntity ColumnEntity::circular(Vec3 position, double diameter, double height) {
  ColumnEntity col;
  col.name = "column";
  const double radius = diameter * 0.5;
  auto& profile = col.model.add_feature(FeatureKind::CircleProfile, {}, {{"radius", radius}});
  col.model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  col.location = std::make_unique<PointLocation>(
      Vec3{position.x, 0.f, position.z}, 0, static_cast<double>(position.y));
  col.sync_from_location(0.0);
  return col;
}

}  // namespace tamias
