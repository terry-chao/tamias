#include "entity/wall_entity.h"

#include "bim/line_location.h"

#include <algorithm>
#include <cmath>

namespace tamias {

WallEntity::WallEntity(Vec3 start, Vec3 end, double thickness, double height)
    : FamilyEntity(EntityKind::Wall, "Basic Wall") {
  name = "wall";
  const Vec3 d = end - start;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);

  // 墙 = RectProfile(width=墙厚, height=墙长) + Extrude(depth=墙高)。
  // 求值器 Z-up→Y-up 后：X=厚、Y=高、Z=长。
  auto& profile = model.add_feature(FeatureKind::RectProfile, {},
                                    {{"width", thickness}, {"height", static_cast<double>(length)}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  location = std::make_unique<LineLocation>(
      Vec3{start.x, 0.f, start.z}, Vec3{end.x, 0.f, end.z}, 0,
      static_cast<double>(start.y));
  sync_from_location(0.0);
}

}  // namespace tamias
