#include "entity/family/host/structural/beam/beam_i_beam_entity.h"

#include <algorithm>
#include <cmath>

namespace tamias {

BeamIBeamEntity::BeamIBeamEntity(Vec3 start, Vec3 end, double flange_width,
                                 double web_thickness, double height, double flange_thickness)
    : start_(start),
      end_(end),
      flange_width_(flange_width),
      web_thickness_(web_thickness),
      height_(height),
      flange_thickness_(flange_thickness) {
  name = "beam";
  local_transform = beam_section_transform(start_, end_);
  model = build_recipe();
}

// 造型：工字形断面（上下翼缘同宽同厚）沿跨度拉伸。
FeatureModel BeamIBeamEntity::build_recipe() const {
  const Vec3 d = end_ - start_;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);

  FeatureModel recipe;
  const auto pts = ibeam_profile_points(flange_width_, web_thickness_, height_, flange_thickness_);
  auto& profile = recipe.add_feature(FeatureKind::PolygonProfile, {}, polygon_params(pts));
  recipe.add_feature(FeatureKind::Extrude, {profile.id},
                     {{"depth", static_cast<double>(length)}});
  return recipe;
}

Result<FeatureModel> BeamIBeamEntity::createGeomImpl() const { return build_recipe(); }

}  // namespace tamias
