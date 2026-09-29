#pragma once

#include "entity/family/family_entity.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace tamias {

// 梁截面子类型。绘制面板先选子类型，再给参数。
enum class BeamShape : std::uint8_t {
  Rectangular = 0,  // 矩形梁（默认）
  Tee = 1,           // T 形梁
  IBeam = 2          // 工字梁
};

// —— 截面造型的公共件（供各截面子类拼自己的配方）——
// 轮廓点写在 Tamias 的 XZ 平面上（y=0，X=宽、Z=高），再由 Extrude 沿 Y 拉伸。
[[nodiscard]] std::vector<Vec3> tee_profile_points(double flange_w, double web_t, double h,
                                                   double flange_t);
[[nodiscard]] std::vector<Vec3> ibeam_profile_points(double flange_w, double web_t, double h,
                                                     double flange_t);
// 把多边形点写成 PolygonProfile 的参数（n, p0x,p0y,p0z, ...）。
[[nodiscard]] std::unordered_map<std::string, double> polygon_params(const std::vector<Vec3>& pts);
// 异形截面的放置：截面在 XZ、拉伸沿 Y(=跨度)，再绕 X 旋 -90° →
// 截面落在 XY（宽×高）、跨度沿 Z，即水平梁。
[[nodiscard]] Mat4 beam_section_transform(Vec3 start, Vec3 end);

// 梁：水平构件，由两端点 + 截面定义。
// 矩形梁：width × depth 截面；T 形/工字梁：翼缘宽 × 腹板厚 × 总高 × 翼缘厚。
//
// 各截面的"造型"由子类重写 createGeomImpl 给出（见 beam_tee_entity.h /
// beam_i_beam_entity.h）；矩形梁直接用 model，不重写。
class BeamEntity : public FamilyEntity {
 public:
  BeamEntity() : FamilyEntity(EntityKind::Beam, "Concrete Beam") {}
  BeamEntity(Vec3 start, Vec3 end, double width, double depth);
};

}  // namespace tamias
