#include "entity/architectural/wall_entity.h"

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

WallEntity WallEntity::hollow(Vec3 start, Vec3 end, double thickness, double height,
                                double leaf, double end_inset, double cap) {
  WallEntity wall;
  wall.name = "wall";
  wall.set_family_type("Hollow Wall");
  const Vec3 d = end - start;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);

  // 外箱：墙厚 × 墙长，拉伸墙高。
  auto& outer_profile = wall.model.add_feature(
      FeatureKind::RectProfile, {},
      {{"width", thickness}, {"height", static_cast<double>(length)}});
  auto& outer_extrude = wall.model.add_feature(FeatureKind::Extrude, {outer_profile.id},
                                                {{"depth", height}});
  // 内箱：更薄（两侧各留 leaf）、更短（两端各留 end_inset）、更矮（顶部留 cap）。
  const double inner_w = std::max(thickness - 2.0 * leaf, 0.05);
  const double inner_h = std::max(static_cast<double>(length) - 2.0 * end_inset, 0.05);
  const double inner_d = std::max(height - cap, 0.05);
  auto& inner_profile = wall.model.add_feature(
      FeatureKind::RectProfile, {},
      {{"width", inner_w}, {"height", inner_h}});
  auto& inner_extrude = wall.model.add_feature(FeatureKind::Extrude, {inner_profile.id},
                                                {{"depth", inner_d}});
  // 外箱减内箱。
  wall.model.add_feature(FeatureKind::Boolean, {outer_extrude.id, inner_extrude.id},
                          {{"operation", static_cast<double>(2)}});  // 2 = Cut

  wall.location = std::make_unique<LineLocation>(
      Vec3{start.x, 0.f, start.z}, Vec3{end.x, 0.f, end.z}, 0,
      static_cast<double>(start.y));
  wall.sync_from_location(0.0);
  return wall;
}

}  // namespace tamias
