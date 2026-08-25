#pragma once

#include "engine/core/result.h"
#include "engine/graphics/mesh.h"
#include "engine/math/math.h"
#include "engine/modeling/feature.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace tamias {

[[nodiscard]] std::unordered_map<std::string, double> line_feature_params(Vec3 a, Vec3 b);
[[nodiscard]] std::unordered_map<std::string, double> polyline_feature_params(
    const std::vector<Vec3>& points);
[[nodiscard]] std::unordered_map<std::string, double> circle_wire_params(Vec3 center,
                                                                         double radius);
[[nodiscard]] std::unordered_map<std::string, double> arc_feature_params(Vec3 start, Vec3 through,
                                                                         Vec3 end);
[[nodiscard]] std::unordered_map<std::string, double> bezier_feature_params(
    const std::vector<Vec3>& points);
[[nodiscard]] std::unordered_map<std::string, double> bezier_feature_params(Vec3 p0, Vec3 p1,
                                                                            Vec3 p2, Vec3 p3);
[[nodiscard]] std::unordered_map<std::string, double> bspline_feature_params(
    const std::vector<Vec3>& points, int degree = 0);
[[nodiscard]] std::unordered_map<std::string, double> nurbs_feature_params(
    const std::vector<Vec3>& points, const std::vector<float>& weights, int degree = 0);
[[nodiscard]] std::unordered_map<std::string, double> rect_wire_params(Vec3 a, Vec3 b);

[[nodiscard]] std::vector<Vec3> sample_line(Vec3 a, Vec3 b);
[[nodiscard]] std::vector<Vec3> sample_polyline(const std::vector<Vec3>& points);
[[nodiscard]] std::vector<Vec3> sample_circle_xz(Vec3 center, float radius, int segments = 64);
[[nodiscard]] std::vector<Vec3> sample_arc_3pt(Vec3 start, Vec3 through, Vec3 end,
                                               int segments = 48);
[[nodiscard]] std::vector<Vec3> sample_quadratic_bezier(Vec3 p0, Vec3 p1, Vec3 p2,
                                                        int segments = 24);
[[nodiscard]] std::vector<Vec3> sample_cubic_bezier(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3,
                                                    int segments = 32);
[[nodiscard]] std::vector<Vec3> sample_bezier(const std::vector<Vec3>& controls, int segments = 0);
[[nodiscard]] int default_spline_degree(std::size_t n_points);
[[nodiscard]] std::vector<Vec3> sample_bspline(const std::vector<Vec3>& controls, int degree = 0,
                                               int segments = 0);
[[nodiscard]] std::vector<Vec3> sample_nurbs(const std::vector<Vec3>& controls,
                                             const std::vector<float>& weights, int degree = 0,
                                             int segments = 0);
[[nodiscard]] std::vector<Vec3> sample_rect_xz(Vec3 a, Vec3 b);
[[nodiscard]] std::vector<Vec3> bezier_control_points(const FeatureModel& model, const Feature& f);
[[nodiscard]] std::vector<Vec3> spline_control_points(const FeatureModel& model, const Feature& f);
[[nodiscard]] std::vector<float> nurbs_weights(const FeatureModel& model, const Feature& f);
[[nodiscard]] int spline_degree(const FeatureModel& model, const Feature& f);
[[nodiscard]] Vec3 feature_xyz(const FeatureModel& model, std::uint64_t feature_id,
                               const std::string& prefix, Vec3 fallback = {});
void set_feature_xyz(FeatureModel& model, std::uint64_t feature_id, const std::string& prefix,
                     Vec3 point);
[[nodiscard]] std::vector<Vec3> polyline_points(const FeatureModel& model, const Feature& f);
[[nodiscard]] std::vector<Vec3> rect_wire_points(const FeatureModel& model, const Feature& f);

[[nodiscard]] std::vector<Vec3> sample_sketch_feature(const FeatureModel& model, const Feature& f);
[[nodiscard]] MeshCpu make_polyline_lines(const std::vector<Vec3>& points);
[[nodiscard]] Result<MeshCpu> mesh_from_sketch_feature(const FeatureModel& model, const Feature& f);

}  // namespace tamias
