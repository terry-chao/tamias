#include "engine/modeling/evaluator.h"

#include "engine/modeling/curve_geom.h"
#include "engine/modeling/edge_fingerprint.h"
#include "engine/modeling/linked_kernels.h"
#include "engine/profile/timing_scope.h"

#include <string>
#include <utility>
#include <vector>

namespace tamias {
namespace {

const char* feature_scope_name(const FeatureModel& model, const Feature& f) {
  if (f.kind == FeatureKind::Boolean) {
    const int op = static_cast<int>(model.param(f.id, "operation", 0.0));
    switch (op) {
      case 1:
        return "Boolean Common";
      case 2:
        return "Boolean Cut";
      case 0:
      default:
        return "Boolean Fuse";
    }
  }
  return feature_kind_name(f.kind);
}

// 一个特征的输入体（inputs[0]）。
Result<BodyRef> input_body(const std::unordered_map<std::uint64_t, BodyRef>& bodies,
                           const Feature& f, const char* what) {
  if (f.inputs.empty()) {
    return Err(std::string(what) + " feature has no shape input");
  }
  const auto it = bodies.find(f.inputs[0]);
  if (it == bodies.end()) {
    return Err(std::string(what) + " references a missing shape");
  }
  return it->second;
}

}  // namespace

Result<std::unordered_map<std::uint64_t, BodyRef>> evaluate_feature_bodies(
    const FeatureModel& model, ModelKernel& kernel, std::uint64_t stop_feature_id) {
  std::unordered_map<std::uint64_t, BodyRef> bodies;
  for (const Feature& f : model.features()) {
    TAMIAS_TIMING_SCOPE(feature_scope_name(model, f), TimingCategory::Modeling);
    BodyRef body;
    switch (f.kind) {
      case FeatureKind::RectProfile: {
        const double w = model.param(f.id, "width", 1.0);
        const double h = model.param(f.id, "height", 1.0);
        auto created = kernel.make_rect_face(w, h);
        if (!created) {
          return Err(created.error());
        }
        body = std::move(*created);
        break;
      }
      case FeatureKind::PolygonProfile: {
        const std::vector<Vec3> pts = polyline_points(model, f);
        if (pts.size() < 3) {
          return Err("PolygonProfile needs at least 3 points");
        }
        auto created = kernel.make_polygon_face(pts);
        if (!created) {
          return Err(created.error());
        }
        body = std::move(*created);
        break;
      }
      case FeatureKind::CircleProfile: {
        const double r = model.param(f.id, "radius", 0.5);
        auto created = kernel.make_circle_face(r);
        if (!created) {
          return Err(created.error());
        }
        body = std::move(*created);
        break;
      }
      case FeatureKind::Extrude: {
        auto input = input_body(bodies, f, "Extrude");
        if (!input) {
          return Err(input.error());
        }
        const double depth = model.param(f.id, "depth", 1.0);
        auto created = kernel.extrude(**input, depth);
        if (!created) {
          return Err(created.error());
        }
        body = std::move(*created);
        break;
      }
      case FeatureKind::Boolean: {
        if (f.inputs.size() < 2) {
          return Err("Boolean feature needs two shape inputs");
        }
        const auto it_a = bodies.find(f.inputs[0]);
        const auto it_b = bodies.find(f.inputs[1]);
        if (it_a == bodies.end() || it_b == bodies.end()) {
          return Err("Boolean references a missing shape");
        }
        const int op = static_cast<int>(model.param(f.id, "operation", 0.0));
        auto created = kernel.boolean(*it_a->second, *it_b->second, static_cast<BooleanOp>(op));
        if (!created) {
          return Err(created.error());
        }
        body = std::move(*created);
        break;
      }
      case FeatureKind::Fillet:
      case FeatureKind::Chamfer: {
        auto input = input_body(bodies, f, feature_kind_name(f.kind));
        if (!input) {
          return Err(input.error());
        }
        // 倒哪条棱：中间层按「索引 + 几何指纹」解析，内核只认 EdgeId。
        auto edge = resolve_edge(kernel, **input, f);
        if (!edge) {
          return Err(edge.error());
        }
        const EdgeId edges[1] = {*edge};
        if (f.kind == FeatureKind::Fillet) {
          const double radius = model.param(f.id, "radius", 0.1);
          auto created = kernel.fillet(**input, edges, radius);
          if (!created) {
            return Err(created.error());
          }
          body = std::move(*created);
        } else {
          const double distance = model.param(f.id, "distance", 0.1);
          auto created = kernel.chamfer(**input, edges, distance);
          if (!created) {
            return Err(created.error());
          }
          body = std::move(*created);
        }
        break;
      }
      case FeatureKind::Transform: {
        auto input = input_body(bodies, f, "Transform");
        if (!input) {
          return Err(input.error());
        }
        const Vec3 translation{static_cast<float>(model.param(f.id, "tx", 0.0)),
                               static_cast<float>(model.param(f.id, "ty", 0.0)),
                               static_cast<float>(model.param(f.id, "tz", 0.0))};
        auto created = kernel.transform(**input, translation);
        if (!created) {
          return Err(created.error());
        }
        body = std::move(*created);
        break;
      }
      case FeatureKind::Cylinder: {
        const double radius = model.param(f.id, "radius", 0.05);
        const double height = model.param(f.id, "height", 0.1);
        const Vec3 center{static_cast<float>(model.param(f.id, "cx", 0.0)),
                          static_cast<float>(model.param(f.id, "cy", 0.0)),
                          static_cast<float>(model.param(f.id, "cz", 0.0))};
        const Vec3 axis{static_cast<float>(model.param(f.id, "ax", 0.0)),
                        static_cast<float>(model.param(f.id, "ay", 1.0)),
                        static_cast<float>(model.param(f.id, "az", 0.0))};
        auto created = kernel.cylinder(radius, height, center, axis);
        if (!created) {
          return Err(created.error());
        }
        body = std::move(*created);
        break;
      }
      default:
        // 草图特征（Line / Arc / …）不是体：只有输出是草图时由
        // evaluate_feature_model 走 mesh_from_sketch_feature。
        return Err("unknown feature kind");
    }
    bodies[f.id] = std::move(body);
    if (stop_feature_id != 0 && f.id == stop_feature_id) {
      break;
    }
  }
  return bodies;
}

Result<MeshCpu> evaluate_feature_model(const FeatureModel& model, ModelKernel& kernel,
                                       double linear_deflection) {
  TAMIAS_TIMING_SCOPE("evaluate_feature_model", TimingCategory::Modeling);
  const Feature* out = model.output_feature();
  if (out != nullptr && is_sketch_feature(out->kind)) {
    TAMIAS_TIMING_SCOPE(feature_kind_name(out->kind), TimingCategory::Modeling);
    return mesh_from_sketch_feature(model, *out);
  }
  auto bodies = evaluate_feature_bodies(model, kernel, 0);
  if (!bodies) {
    return Err(bodies.error());
  }
  if (out == nullptr) {
    return Err("feature model has no features");
  }
  const auto it = bodies->find(out->id);
  if (it == bodies->end()) {
    return Err("output feature has no shape");
  }
  return kernel.tessellate(*it->second, linear_deflection);
}

Result<MeshCpu> evaluate_feature_model(const FeatureModel& model, double linear_deflection) {
  auto kernel = default_kernel();
  if (!kernel) {
    return Err(kernel.error());
  }
  return evaluate_feature_model(model, **kernel, linear_deflection);
}

}  // namespace tamias
