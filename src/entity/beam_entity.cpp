#include "entity/beam_entity.h"

#include <algorithm>
#include <cmath>

namespace tamias {

BeamEntity::BeamEntity(Vec3 start, Vec3 end, double width, double depth)
    : FamilyEntity(EntityKind::Beam, "Concrete Beam") {
  name = "beam";
  const Vec3 d = end - start;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);
  const Vec3 mid = (start + end) * 0.5f;
  const float yaw = std::atan2(d.x, d.z);

  auto& profile = model.add_feature(FeatureKind::RectProfile, {},
                                    {{"width", width}, {"height", static_cast<double>(length)}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", depth}});
  local_transform = translate(mid) * rotate_y(yaw);
}

}  // namespace tamias
