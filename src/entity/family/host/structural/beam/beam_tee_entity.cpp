#include "entity/family/host/structural/beam/beam_tee_entity.h"

#include <algorithm>
#include <cmath>

namespace tamias {

BeamTeeEntity::BeamTeeEntity(Vec3 start, Vec3 end, double flange_width, double web_thickness,
                             double height, double flange_thickness)
    : start_(start),
      end_(end),
      flange_width_(flange_width),
      web_thickness_(web_thickness),
      height_(height),
      flange_thickness_(flange_thickness) {
  name = "beam";
  local_transform = beam_section_transform(start_, end_);
  // 持久化副本：反序列化 / clone 之后只有它能把形状带回来。
  model = build_recipe();
}

// 造型：T 形断面（翼缘在上、腹板在下）沿跨度拉伸。
FeatureModel BeamTeeEntity::build_recipe() const {
  const Vec3 d = end_ - start_;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);

  FeatureModel recipe;
  const auto pts = tee_profile_points(flange_width_, web_thickness_, height_, flange_thickness_);
  auto& profile = recipe.add_feature(FeatureKind::PolygonProfile, {}, polygon_params(pts));
  recipe.add_feature(FeatureKind::Extrude, {profile.id},
                     {{"depth", static_cast<double>(length)}});
  return recipe;
}

Result<FeatureModel> BeamTeeEntity::createGeomImpl() const { return build_recipe(); }

}  // namespace tamias
