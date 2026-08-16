#include "entity/wall_entity.h"

#include <algorithm>
#include <cmath>

namespace tamias {

WallEntity::WallEntity(Vec3 start, Vec3 end, double thickness, double height) {
  kind_ = EntityKind::Wall;
  name = "wall";
  const Vec3 d = end - start;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);
  const Vec3 mid = (start + end) * 0.5f;
  const float yaw = std::atan2(d.x, d.z);

  // 墙 = RectProfile(width=墙厚, height=墙长) + Extrude(depth=墙高)。
  // 求值器 Z-up→Y-up 后：X=厚、Y=高、Z=长。
  auto& profile = model.add_feature(FeatureKind::RectProfile, {},
                                    {{"width", thickness}, {"height", static_cast<double>(length)}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  local_transform = translate(mid) * rotate_y(yaw);
}

}  // namespace tamias
