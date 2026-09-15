#pragma once

#include "entity/family/family_entity.h"

namespace tamias {

// 梁截面子类型。绘制面板先选子类型，再给参数。
enum class BeamShape : std::uint8_t {
  Rectangular = 0,  // 矩形梁（默认）
  Tee = 1,           // T 形梁
  IBeam = 2          // 工字梁
};

// 梁：水平构件，由两端点 + 截面定义。
// 矩形梁：width × depth 截面；T 形/工字梁：翼缘宽 × 腹板厚 × 总高 × 翼缘厚。
class BeamEntity final : public FamilyEntity {
 public:
  BeamEntity() : FamilyEntity(EntityKind::Beam, "Concrete Beam") {}
  BeamEntity(Vec3 start, Vec3 end, double width, double depth);
  // T 形梁：flange_width 翼缘宽，web_thickness 腹板厚，height 总高，flange_thickness 翼缘厚。
  static BeamEntity tee(Vec3 start, Vec3 end, double flange_width, double web_thickness,
                         double height, double flange_thickness);
  // 工字梁：上下翼缘同宽同厚。
  static BeamEntity ibeam(Vec3 start, Vec3 end, double flange_width, double web_thickness,
                          double height, double flange_thickness);
};

}  // namespace tamias
