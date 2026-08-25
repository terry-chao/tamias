#include "curve_geom.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace tamias {
namespace {

constexpr float kPi = 3.14159265358979f;

void put_xyz(std::unordered_map<std::string, double>& params, const std::string& prefix, Vec3 p) {
  params[prefix + "x"] = static_cast<double>(p.x);
  params[prefix + "y"] = static_cast<double>(p.y);
  params[prefix + "z"] = static_cast<double>(p.z);
}

Vec3 get_xyz(const FeatureModel& model, std::uint64_t id, const std::string& prefix, Vec3 fb = {}) {
  return {static_cast<float>(model.param(id, prefix + "x", static_cast<double>(fb.x))),
          static_cast<float>(model.param(id, prefix + "y", static_cast<double>(fb.y))),
          static_cast<float>(model.param(id, prefix + "z", static_cast<double>(fb.z)))};
}

std::vector<Vec3> polyline_from_params(const FeatureModel& model, std::uint64_t id) {
  const int n = std::max(0, static_cast<int>(model.param(id, "n", 0.0)));
  std::vector<Vec3> points;
  points.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    points.push_back(get_xyz(model, id, "p" + std::to_string(i)));
  }
  return points;
}

float wrap_tau(float a) {
  while (a < 0.f) {
    a += 2.f * kPi;
  }
  while (a >= 2.f * kPi) {
    a -= 2.f * kPi;
  }
  return a;
}

}  // namespace

std::unordered_map<std::string, double> line_feature_params(Vec3 a, Vec3 b) {
  std::unordered_map<std::string, double> params;
  put_xyz(params, "a", a);
  put_xyz(params, "b", b);
  return params;
}

std::unordered_map<std::string, double> polyline_feature_params(const std::vector<Vec3>& points) {
  std::unordered_map<std::string, double> params;
  params["n"] = static_cast<double>(points.size());
  for (std::size_t i = 0; i < points.size(); ++i) {
    put_xyz(params, "p" + std::to_string(i), points[i]);
  }
  return params;
}

std::unordered_map<std::string, double> circle_wire_params(Vec3 center, double radius) {
  std::unordered_map<std::string, double> params;
  put_xyz(params, "c", center);
  params["radius"] = radius;
  return params;
}

std::unordered_map<std::string, double> arc_feature_params(Vec3 start, Vec3 through, Vec3 end) {
  std::unordered_map<std::string, double> params;
  put_xyz(params, "a", start);
  put_xyz(params, "b", through);
  put_xyz(params, "c", end);
  return params;
}

std::unordered_map<std::string, double> bezier_feature_params(const std::vector<Vec3>& points) {
  std::unordered_map<std::string, double> params;
  params["n"] = static_cast<double>(points.size());
  for (std::size_t i = 0; i < points.size(); ++i) {
    put_xyz(params, "p" + std::to_string(i), points[i]);
  }
  return params;
}

std::unordered_map<std::string, double> bezier_feature_params(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3) {
  return bezier_feature_params(std::vector<Vec3>{p0, p1, p2, p3});
}

int default_spline_degree(std::size_t n_points) {
  if (n_points <= 1) {
    return 1;
  }
  return std::min(3, static_cast<int>(n_points - 1));
}

namespace {

int clamp_spline_degree(std::size_t n_points, int degree) {
  const int max_p = n_points <= 1 ? 1 : static_cast<int>(n_points - 1);
  int p = degree > 0 ? degree : default_spline_degree(n_points);
  if (p < 1) {
    p = 1;
  }
  if (p > max_p) {
    p = max_p;
  }
  return p;
}

}  // namespace

std::unordered_map<std::string, double> bspline_feature_params(const std::vector<Vec3>& points,
                                                               int degree) {
  auto params = polyline_feature_params(points);
  params["degree"] = static_cast<double>(clamp_spline_degree(points.size(), degree));
  return params;
}

std::unordered_map<std::string, double> nurbs_feature_params(const std::vector<Vec3>& points,
                                                             const std::vector<float>& weights,
                                                             int degree) {
  auto params = bspline_feature_params(points, degree);
  for (std::size_t i = 0; i < points.size(); ++i) {
    const float w = i < weights.size() ? weights[i] : 1.f;
    params["w" + std::to_string(i)] = static_cast<double>(w);
  }
  return params;
}

std::unordered_map<std::string, double> rect_wire_params(Vec3 a, Vec3 b) {
  std::unordered_map<std::string, double> params;
  put_xyz(params, "a", a);
  put_xyz(params, "b", b);
  return params;
}

std::vector<Vec3> sample_line(Vec3 a, Vec3 b) { return {a, b}; }

std::vector<Vec3> sample_polyline(const std::vector<Vec3>& points) { return points; }

std::vector<Vec3> sample_circle_xz(Vec3 center, float radius, int segments) {
  const int n = std::max(8, segments);
  const float r = std::max(radius, 1e-4f);
  std::vector<Vec3> pts;
  pts.reserve(static_cast<std::size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) {
    const float a = 2.f * kPi * static_cast<float>(i) / static_cast<float>(n);
    pts.push_back({center.x + r * std::cos(a), center.y, center.z + r * std::sin(a)});
  }
  return pts;
}

std::vector<Vec3> sample_arc_3pt(Vec3 start, Vec3 through, Vec3 end, int segments) {
  const Vec3 ab = through - start;
  const Vec3 ac = end - start;
  const Vec3 normal = cross(ab, ac);
  const float n2 = dot(normal, normal);
  if (n2 < 1e-12f) {
    return {start, through, end};
  }

  const Vec3 to_center =
      (cross(normal, ab) * dot(ac, ac) + cross(ac, normal) * dot(ab, ab)) * (0.5f / n2);
  const Vec3 center = start + to_center;
  const float radius = length(to_center);
  if (radius < 1e-5f) {
    return {start, through, end};
  }

  const Vec3 u = normalize(start - center);
  const Vec3 w = normalize(normal);
  const Vec3 v = cross(w, u);
  const auto angle_of = [&](Vec3 p) {
    const Vec3 d = p - center;
    return std::atan2(dot(d, v), dot(d, u));
  };
  const float ang_through = wrap_tau(angle_of(through));
  const float ang_end = wrap_tau(angle_of(end));
  const bool through_on_ccw = ang_through <= ang_end + 1e-5f;
  float sweep = through_on_ccw ? ang_end : ang_end - 2.f * kPi;

  const int count = std::max(8, segments);
  std::vector<Vec3> pts;
  pts.reserve(static_cast<std::size_t>(count) + 1);
  for (int i = 0; i <= count; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(count);
    const float a = sweep * t;
    const float c = std::cos(a);
    const float s = std::sin(a);
    pts.push_back(center + u * (radius * c) + v * (radius * s));
  }
  return pts;
}

std::vector<Vec3> sample_quadratic_bezier(Vec3 p0, Vec3 p1, Vec3 p2, int segments) {
  const int n = std::max(4, segments);
  std::vector<Vec3> pts;
  pts.reserve(static_cast<std::size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(n);
    const float u = 1.f - t;
    const float b0 = u * u;
    const float b1 = 2.f * u * t;
    const float b2 = t * t;
    pts.push_back(p0 * b0 + p1 * b1 + p2 * b2);
  }
  return pts;
}

std::vector<Vec3> sample_cubic_bezier(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, int segments) {
  return sample_bezier({p0, p1, p2, p3}, segments);
}

namespace {

Vec3 eval_bezier(const std::vector<Vec3>& ctrls, float t) {
  if (ctrls.empty()) {
    return {};
  }
  if (ctrls.size() == 1) {
    return ctrls.front();
  }
  std::vector<Vec3> work = ctrls;
  const float u = 1.f - t;
  for (std::size_t k = work.size(); k > 1; --k) {
    for (std::size_t i = 0; i + 1 < k; ++i) {
      work[i] = work[i] * u + work[i + 1] * t;
    }
  }
  return work.empty() ? Vec3{} : work.front();
}

}  // namespace

std::vector<Vec3> sample_bezier(const std::vector<Vec3>& controls, int segments) {
  if (controls.size() < 2) {
    return controls;
  }
  const int n = segments > 0
                    ? std::max(4, segments)
                    : std::max(16, static_cast<int>(controls.size() - 1) * 12);
  std::vector<Vec3> pts;
  pts.reserve(static_cast<std::size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(n);
    pts.push_back(eval_bezier(controls, t));
  }
  return pts;
}

std::vector<Vec3> bezier_control_points(const FeatureModel& model, const Feature& f) {
  if (f.kind != FeatureKind::Bezier) {
    return {};
  }
  const int n = static_cast<int>(model.param(f.id, "n", 0.0));
  if (n >= 2) {
    return polyline_from_params(model, f.id);
  }
  return {get_xyz(model, f.id, "p0"), get_xyz(model, f.id, "p1"), get_xyz(model, f.id, "p2"),
          get_xyz(model, f.id, "p3")};
}

std::vector<Vec3> spline_control_points(const FeatureModel& model, const Feature& f) {
  if (f.kind == FeatureKind::Bezier) {
    return bezier_control_points(model, f);
  }
  if (f.kind != FeatureKind::BSpline && f.kind != FeatureKind::Nurbs) {
    return {};
  }
  return polyline_from_params(model, f.id);
}

std::vector<float> nurbs_weights(const FeatureModel& model, const Feature& f) {
  const std::vector<Vec3> ctrls = spline_control_points(model, f);
  std::vector<float> weights;
  weights.reserve(ctrls.size());
  for (std::size_t i = 0; i < ctrls.size(); ++i) {
    weights.push_back(static_cast<float>(model.param(f.id, "w" + std::to_string(i), 1.0)));
  }
  return weights;
}

int spline_degree(const FeatureModel& model, const Feature& f) {
  const std::size_t n = spline_control_points(model, f).size();
  return clamp_spline_degree(n, static_cast<int>(model.param(f.id, "degree", 0.0)));
}

namespace {

std::vector<float> clamped_uniform_knots(int n, int p) {
  const int m = n + p + 1;
  std::vector<float> U(static_cast<std::size_t>(m + 1), 0.f);
  for (int i = 0; i <= p; ++i) {
    U[static_cast<std::size_t>(i)] = 0.f;
  }
  const int interior = n - p;
  for (int j = 1; j <= interior; ++j) {
    U[static_cast<std::size_t>(p + j)] =
        static_cast<float>(j) / static_cast<float>(interior + 1);
  }
  for (int i = m - p; i <= m; ++i) {
    U[static_cast<std::size_t>(i)] = 1.f;
  }
  return U;
}

int find_knot_span(int n, int p, float u, const std::vector<float>& U) {
  if (u >= U[static_cast<std::size_t>(n + 1)]) {
    return n;
  }
  if (u <= U[static_cast<std::size_t>(p)]) {
    return p;
  }
  int low = p;
  int high = n + 1;
  int mid = (low + high) / 2;
  while (u < U[static_cast<std::size_t>(mid)] || u >= U[static_cast<std::size_t>(mid + 1)]) {
    if (u < U[static_cast<std::size_t>(mid)]) {
      high = mid;
    } else {
      low = mid;
    }
    mid = (low + high) / 2;
  }
  return mid;
}

std::vector<float> basis_funs(int span, float u, int p, const std::vector<float>& U) {
  std::vector<float> N(static_cast<std::size_t>(p + 1), 0.f);
  std::vector<float> left(static_cast<std::size_t>(p + 1), 0.f);
  std::vector<float> right(static_cast<std::size_t>(p + 1), 0.f);
  N[0] = 1.f;
  for (int j = 1; j <= p; ++j) {
    left[static_cast<std::size_t>(j)] = u - U[static_cast<std::size_t>(span + 1 - j)];
    right[static_cast<std::size_t>(j)] = U[static_cast<std::size_t>(span + j)] - u;
    float saved = 0.f;
    for (int r = 0; r < j; ++r) {
      const float denom =
          right[static_cast<std::size_t>(r + 1)] + left[static_cast<std::size_t>(j - r)];
      const float temp = std::abs(denom) < 1e-12f ? 0.f : N[static_cast<std::size_t>(r)] / denom;
      N[static_cast<std::size_t>(r)] = saved + right[static_cast<std::size_t>(r + 1)] * temp;
      saved = left[static_cast<std::size_t>(j - r)] * temp;
    }
    N[static_cast<std::size_t>(j)] = saved;
  }
  return N;
}

Vec3 eval_nurbs(const std::vector<Vec3>& ctrls, const std::vector<float>& weights, int degree,
                float u) {
  if (ctrls.empty()) {
    return {};
  }
  if (ctrls.size() == 1) {
    return ctrls.front();
  }
  const int n = static_cast<int>(ctrls.size()) - 1;
  const int p = clamp_spline_degree(ctrls.size(), degree);
  const std::vector<float> U = clamped_uniform_knots(n, p);
  const float t = std::clamp(u, 0.f, 1.f);
  if (t <= 0.f) {
    return ctrls.front();
  }
  if (t >= 1.f) {
    return ctrls.back();
  }
  const int span = find_knot_span(n, p, t, U);
  const std::vector<float> N = basis_funs(span, t, p, U);
  Vec3 num{};
  float den = 0.f;
  for (int i = 0; i <= p; ++i) {
    const int idx = span - p + i;
    if (idx < 0 || idx > n) {
      continue;
    }
    const float w = idx < static_cast<int>(weights.size()) ? weights[static_cast<std::size_t>(idx)]
                                                           : 1.f;
    const float nw = N[static_cast<std::size_t>(i)] * w;
    num = num + ctrls[static_cast<std::size_t>(idx)] * nw;
    den += nw;
  }
  if (std::abs(den) < 1e-12f) {
    return ctrls.front();
  }
  return num * (1.f / den);
}

int spline_sample_count(std::size_t n_points, int segments) {
  if (segments > 0) {
    return std::max(4, segments);
  }
  return std::max(16, static_cast<int>(n_points <= 1 ? 1 : n_points - 1) * 16);
}

}  // namespace

std::vector<Vec3> sample_bspline(const std::vector<Vec3>& controls, int degree, int segments) {
  if (controls.size() < 2) {
    return controls;
  }
  std::vector<float> ones(controls.size(), 1.f);
  const int n = spline_sample_count(controls.size(), segments);
  std::vector<Vec3> pts;
  pts.reserve(static_cast<std::size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(n);
    pts.push_back(eval_nurbs(controls, ones, degree, t));
  }
  return pts;
}

std::vector<Vec3> sample_nurbs(const std::vector<Vec3>& controls, const std::vector<float>& weights,
                               int degree, int segments) {
  if (controls.size() < 2) {
    return controls;
  }
  const int n = spline_sample_count(controls.size(), segments);
  std::vector<Vec3> pts;
  pts.reserve(static_cast<std::size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(n);
    pts.push_back(eval_nurbs(controls, weights, degree, t));
  }
  return pts;
}

Vec3 feature_xyz(const FeatureModel& model, std::uint64_t feature_id, const std::string& prefix,
                 Vec3 fallback) {
  return get_xyz(model, feature_id, prefix, fallback);
}

void set_feature_xyz(FeatureModel& model, std::uint64_t feature_id, const std::string& prefix,
                     Vec3 point) {
  model.set_param(feature_id, prefix + "x", static_cast<double>(point.x));
  model.set_param(feature_id, prefix + "y", static_cast<double>(point.y));
  model.set_param(feature_id, prefix + "z", static_cast<double>(point.z));
}

std::vector<Vec3> polyline_points(const FeatureModel& model, const Feature& f) {
  return polyline_from_params(model, f.id);
}

std::vector<Vec3> sample_rect_xz(Vec3 a, Vec3 b) {
  const float y = a.y;
  const Vec3 p0{a.x, y, a.z};
  const Vec3 p1{b.x, y, a.z};
  const Vec3 p2{b.x, y, b.z};
  const Vec3 p3{a.x, y, b.z};
  return {p0, p1, p2, p3, p0};
}

std::vector<Vec3> sample_sketch_feature(const FeatureModel& model, const Feature& f) {
  switch (f.kind) {
    case FeatureKind::Line:
      return sample_line(get_xyz(model, f.id, "a"), get_xyz(model, f.id, "b"));
    case FeatureKind::Polyline:
      return sample_polyline(polyline_from_params(model, f.id));
    case FeatureKind::CircleWire: {
      const Vec3 c = get_xyz(model, f.id, "c");
      const float r = static_cast<float>(model.param(f.id, "radius", 1.0));
      return sample_circle_xz(c, r);
    }
    case FeatureKind::Arc:
      return sample_arc_3pt(get_xyz(model, f.id, "a"), get_xyz(model, f.id, "b"),
                            get_xyz(model, f.id, "c"));
    case FeatureKind::Bezier:
      return sample_bezier(bezier_control_points(model, f));
    case FeatureKind::BSpline:
      return sample_bspline(spline_control_points(model, f), spline_degree(model, f));
    case FeatureKind::Nurbs:
      return sample_nurbs(spline_control_points(model, f), nurbs_weights(model, f),
                          spline_degree(model, f));
    case FeatureKind::RectWire:
      return sample_rect_xz(get_xyz(model, f.id, "a"), get_xyz(model, f.id, "b"));
    default:
      return {};
  }
}

MeshCpu make_polyline_lines(const std::vector<Vec3>& points) {
  MeshCpu mesh;
  mesh.line_list = true;
  if (points.size() < 2) {
    return mesh;
  }

  mesh.vertices.reserve(points.size());
  for (const Vec3& p : points) {
    Vertex v{};
    v.position = p;
    v.normal = {0.f, 1.f, 0.f};
    mesh.vertices.push_back(v);
  }
  mesh.indices.reserve((points.size() - 1) * 2);
  for (std::size_t i = 0; i + 1 < points.size(); ++i) {
    if (length(points[i + 1] - points[i]) < 1e-5f) {
      continue;
    }
    mesh.indices.push_back(static_cast<std::uint32_t>(i));
    mesh.indices.push_back(static_cast<std::uint32_t>(i + 1));
  }
  recompute_bounds(mesh);
  if (mesh.bounds.valid()) {
    const Vec3 pad{kSketchPickRadius, kSketchPickRadius, kSketchPickRadius};
    mesh.bounds.min = mesh.bounds.min - pad;
    mesh.bounds.max = mesh.bounds.max + pad;
  }
  return mesh;
}

Result<MeshCpu> mesh_from_sketch_feature(const FeatureModel& model, const Feature& f) {
  const std::vector<Vec3> pts = sample_sketch_feature(model, f);
  if (pts.size() < 2) {
    return Err("sketch feature produced no curve");
  }
  MeshCpu mesh = make_polyline_lines(pts);
  if (mesh.indices.empty()) {
    return Err("sketch feature produced an empty mesh");
  }
  return mesh;
}

}  // namespace tamias
