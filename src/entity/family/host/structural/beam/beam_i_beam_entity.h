#pragma once

#include "entity/family/host/structural/beam/beam_entity.h"

namespace tamias {

// 工字梁：上下翼缘同宽同厚。和 BeamTeeEntity 一个套路——造型写在
// createGeomImpl() 里，构造函数把同一份配方写进 model 供持久化。
class BeamIBeamEntity final : public BeamEntity {
 public:
  BeamIBeamEntity(Vec3 start, Vec3 end, double flange_width, double web_thickness, double height,
                  double flange_thickness);

 protected:
  [[nodiscard]] Result<FeatureModel> createGeomImpl() const override;

 private:
  [[nodiscard]] FeatureModel build_recipe() const;

  Vec3 start_{};
  Vec3 end_{};
  double flange_width_ = 0.4;
  double web_thickness_ = 0.2;
  double height_ = 0.5;
  double flange_thickness_ = 0.1;
};

}  // namespace tamias
