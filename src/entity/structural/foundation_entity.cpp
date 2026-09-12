#include "entity/structural/foundation_entity.h"

#include "bim/point_location.h"

namespace tamias {

FoundationEntity::FoundationEntity(Vec3 position, double length, double width, double height)
    : FamilyEntity(EntityKind::Foundation, "Isolated Footing") {
  name = "foundation";
  auto& profile = model.add_feature(FeatureKind::RectProfile, {},
                                     {{"width", length}, {"height", width}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  location = std::make_unique<PointLocation>(
      Vec3{position.x, 0.f, position.z}, 0, static_cast<double>(position.y));
  sync_from_location(0.0);
}

FoundationEntity FoundationEntity::pile(Vec3 position, double diameter, double height) {
  FoundationEntity f;
  f.name = "foundation";
  const double radius = diameter * 0.5;
  auto& profile = f.model.add_feature(FeatureKind::CircleProfile, {}, {{"radius", radius}});
  f.model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  f.location = std::make_unique<PointLocation>(
      Vec3{position.x, 0.f, position.z}, 0, static_cast<double>(position.y));
  f.sync_from_location(0.0);
  return f;
}

}  // namespace tamias
