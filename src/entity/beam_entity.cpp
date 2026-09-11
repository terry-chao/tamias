#include "entity/beam_entity.h"

#include <algorithm>
#include <cmath>

namespace tamias {
namespace {

// 求值器坐标：PolygonProfile 点 (tx, 0, tz) 拉伸后得 Tamias (X=tx, Y=depth, Z=tz)。
// 截面在 XZ 平面（X=宽, Z=高），拉伸 depth=跨度 → Tamias (X=宽, Y=跨度, Z=高)，
// 再绕 X 旋转 -90°（(x,y,z)→(x,z,-y)）→ (X=宽, Y=高, Z=-跨度)，即水平梁。
constexpr float kHalfPi = 1.57079632679489661923f;

// 构造 T 形截面多边形点（Tamias XZ 平面，y=0）。
// 翼缘在上：X∈[-flange_w/2, flange_w/2], Z∈[h-flange_t, h]；腹板在下：X∈[-web_t/2, web_t/2], Z∈[0, h-flange_t]。
std::vector<Vec3> tee_profile_points(double flange_w, double web_t, double h, double flange_t) {
  const double hw = flange_w * 0.5;
  const double ht = web_t * 0.5;
  const double z_web = h - flange_t;  // 腹板顶 = 翼缘底
  return {
      {-static_cast<float>(ht), 0.f, 0.f},
      {-static_cast<float>(ht), 0.f, static_cast<float>(z_web)},
      {-static_cast<float>(hw), 0.f, static_cast<float>(z_web)},
      {-static_cast<float>(hw), 0.f, static_cast<float>(h)},
      {static_cast<float>(hw), 0.f, static_cast<float>(h)},
      {static_cast<float>(hw), 0.f, static_cast<float>(z_web)},
      {static_cast<float>(ht), 0.f, static_cast<float>(z_web)},
      {static_cast<float>(ht), 0.f, 0.f},
  };
}

// 构造工字截面多边形点（上下翼缘同宽同厚）。
std::vector<Vec3> ibeam_profile_points(double flange_w, double web_t, double h, double flange_t) {
  const double hw = flange_w * 0.5;
  const double ht = web_t * 0.5;
  const double z_bot = flange_t;          // 下翼缘顶 = 腹板底
  const double z_top = h - flange_t;      // 上翼缘底 = 腹板顶
  return {
      {-static_cast<float>(ht), 0.f, 0.f},
      {-static_cast<float>(hw), 0.f, 0.f},
      {-static_cast<float>(hw), 0.f, static_cast<float>(z_bot)},
      {-static_cast<float>(ht), 0.f, static_cast<float>(z_bot)},
      {-static_cast<float>(ht), 0.f, static_cast<float>(z_top)},
      {-static_cast<float>(hw), 0.f, static_cast<float>(z_top)},
      {-static_cast<float>(hw), 0.f, static_cast<float>(h)},
      {static_cast<float>(hw), 0.f, static_cast<float>(h)},
      {static_cast<float>(hw), 0.f, static_cast<float>(z_top)},
      {static_cast<float>(ht), 0.f, static_cast<float>(z_top)},
      {static_cast<float>(ht), 0.f, static_cast<float>(z_bot)},
      {static_cast<float>(hw), 0.f, static_cast<float>(z_bot)},
      {static_cast<float>(hw), 0.f, 0.f},
  };
}

// 把多边形点写入 PolygonProfile 参数（n, p0x,p0y,p0z, ...）。
std::unordered_map<std::string, double> polygon_params(const std::vector<Vec3>& pts) {
  std::unordered_map<std::string, double> params;
  params["n"] = static_cast<double>(pts.size());
  for (std::size_t i = 0; i < pts.size(); ++i) {
    const std::string base = "p" + std::to_string(i);
    params[base + "x"] = static_cast<double>(pts[i].x);
    params[base + "y"] = static_cast<double>(pts[i].y);
    params[base + "z"] = static_cast<double>(pts[i].z);
  }
  return params;
}

}  // namespace

BeamEntity::BeamEntity(Vec3 start, Vec3 end, double width, double depth)
    : FamilyEntity(EntityKind::Beam, "Concrete Beam") {
  name = "beam";
  const Vec3 d = end - start;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);
  const Vec3 mid = (start + end) * 0.5f;
  const float yaw = std::atan2(d.x, d.z);

  // 矩形梁：RectProfile(width=宽, height=跨度) + Extrude(depth=梁高)。
  auto& profile = model.add_feature(FeatureKind::RectProfile, {},
                                    {{"width", width}, {"height", static_cast<double>(length)}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", depth}});
  local_transform = translate(mid) * rotate_y(yaw);
}

BeamEntity BeamEntity::tee(Vec3 start, Vec3 end, double flange_width, double web_thickness,
                             double height, double flange_thickness) {
  BeamEntity beam;
  beam.name = "beam";
  const Vec3 d = end - start;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);
  const Vec3 mid = (start + end) * 0.5f;
  const float yaw = std::atan2(d.x, d.z);

  auto pts = tee_profile_points(flange_width, web_thickness, height, flange_thickness);
  auto& profile = beam.model.add_feature(FeatureKind::PolygonProfile, {}, polygon_params(pts));
  beam.model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", static_cast<double>(length)}});
  // 截面在 XZ、拉伸沿 Y(=跨度)，绕 X 旋 -90° → 截面在 XY(=宽×高)、跨度沿 Z。
  beam.local_transform = translate(mid) * rotate_y(yaw) * rotate_x(-kHalfPi);
  return beam;
}

BeamEntity BeamEntity::ibeam(Vec3 start, Vec3 end, double flange_width, double web_thickness,
                              double height, double flange_thickness) {
  BeamEntity beam;
  beam.name = "beam";
  const Vec3 d = end - start;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);
  const Vec3 mid = (start + end) * 0.5f;
  const float yaw = std::atan2(d.x, d.z);

  auto pts = ibeam_profile_points(flange_width, web_thickness, height, flange_thickness);
  auto& profile = beam.model.add_feature(FeatureKind::PolygonProfile, {}, polygon_params(pts));
  beam.model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", static_cast<double>(length)}});
  beam.local_transform = translate(mid) * rotate_y(yaw) * rotate_x(-kHalfPi);
  return beam;
}

}  // namespace tamias
