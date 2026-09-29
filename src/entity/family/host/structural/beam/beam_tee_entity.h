#pragma once

#include "entity/family/host/structural/beam/beam_entity.h"

namespace tamias {

// T 形梁：造型在 createGeomImpl() 里现算（由自己的截面参数写出配方），
// 构造函数把同一份配方写进 model —— model 是持久化载体，反序列化 / clone
// 出来的是基类 BeamEntity，只能靠它还原形状（见 Entity::createGeomImpl 注释）。
//
// 参数：flange_width 翼缘宽，web_thickness 腹板厚，height 总高，flange_thickness 翼缘厚。
class BeamTeeEntity final : public BeamEntity {
 public:
  BeamTeeEntity(Vec3 start, Vec3 end, double flange_width, double web_thickness, double height,
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
