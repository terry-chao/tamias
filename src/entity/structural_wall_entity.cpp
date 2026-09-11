#include "entity/structural_wall_entity.h"

#include "bim/line_location.h"

#include <algorithm>
#include <cmath>

namespace tamias {

StructuralWallEntity::StructuralWallEntity(Vec3 start, Vec3 end, double thickness, double height)
    : FamilyEntity(EntityKind::StructuralWall, "Structural Wall") {
  name = "structural_wall";
  const Vec3 d = end - start;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);

  auto& profile = model.add_feature(FeatureKind::RectProfile, {},
                                    {{"width", thickness}, {"height", static_cast<double>(length)}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  location = std::make_unique<LineLocation>(
      Vec3{start.x, 0.f, start.z}, Vec3{end.x, 0.f, end.z}, 0,
      static_cast<double>(start.y));
  sync_from_location(0.0);
}

}  // namespace tamias
