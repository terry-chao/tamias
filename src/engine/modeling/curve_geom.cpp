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
