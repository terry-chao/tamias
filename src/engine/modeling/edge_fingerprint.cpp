#include "engine/modeling/edge_fingerprint.h"

#include "engine/modeling/evaluator.h"
#include "engine/modeling/linked_kernels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace tamias {
namespace {

// 指纹占用的参数键。write / erase / 属性面板过滤都以这张表为准。
constexpr std::array<const char*, 14> kFingerprintKeys = {
    "edge_fp_version", "edge_mid_x", "edge_mid_y", "edge_mid_z", "edge_dir_x", "edge_dir_y",
    "edge_dir_z",      "edge_len",   "edge_n1_x",  "edge_n1_y",  "edge_n1_z",  "edge_n2_x",
    "edge_n2_y",       "edge_n2_z"};

// 指纹版本：
//   2 = Tamias Y-up 空间 + 中间层归一化（当前，跨后端可比）
//   1 = 早期 OCCT 空间指纹（没有 edge_fp_version 键）——按「没有指纹」处理，
//       退回纯索引行为，不报错。
constexpr double kFingerprintVersion = 2.0;

// 匹配代价阈值。越大越像；超过 kEdgeMatchCost 就认定「不是同一条边」。
constexpr double kEdgeMatchCost = 0.6;
constexpr double kEdgeMatchMargin = 0.10;  // 最优与次优太接近 → 不敢认，报错
constexpr double kEdgeMatchSlack = 0.05;   // 索引候选和最优差不多时，优先信索引

// 内部签名：位置/方向/长度 + 相邻面法线，都是无量纲或单位量。
struct Signature {
  bool valid = false;
  Vec3 mid{};
  Vec3 dir{};
  bool has_dir = false;
  double length = 0.0;
  Vec3 normal1{};
  Vec3 normal2{};
  bool has_normal1 = false;
  bool has_normal2 = false;
};

// 把内核量出来的边变成可比签名：位置按包围盒归一化，长度按对角线归一化。
Signature signature_from_measure(const EdgeMeasure& measure, const Aabb& bounds) {
  Signature sig;
  sig.valid = true;
  const float dx = std::max(bounds.max.x - bounds.min.x, 1e-9f);
  const float dy = std::max(bounds.max.y - bounds.min.y, 1e-9f);
  const float dz = std::max(bounds.max.z - bounds.min.z, 1e-9f);
  sig.mid = Vec3{(measure.mid.x - bounds.min.x) / dx, (measure.mid.y - bounds.min.y) / dy,
                 (measure.mid.z - bounds.min.z) / dz};
  const double diag = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy +
                                static_cast<double>(dz) * dz);
  sig.length = measure.length / (diag > 1e-9 ? diag : 1.0);
  sig.dir = measure.dir;
  sig.has_dir = measure.has_dir;
  sig.normal1 = measure.normal1;
  sig.normal2 = measure.normal2;
  sig.has_normal1 = measure.has_normal1;
  sig.has_normal2 = measure.has_normal2;
  return sig;
}

Signature signature_from_params(const Feature& f) {
  const auto get = [&f](const char* key, double fallback) {
    const auto it = f.params.find(key);
    return it == f.params.end() ? fallback : it->second;
  };
  Signature sig;
  sig.valid = true;
  sig.mid = Vec3{static_cast<float>(get("edge_mid_x", 0.0)),
                 static_cast<float>(get("edge_mid_y", 0.0)),
                 static_cast<float>(get("edge_mid_z", 0.0))};
  sig.length = get("edge_len", 0.0);
  if (f.params.find("edge_dir_x") != f.params.end()) {
    sig.dir = Vec3{static_cast<float>(get("edge_dir_x", 0.0)),
                   static_cast<float>(get("edge_dir_y", 0.0)),
                   static_cast<float>(get("edge_dir_z", 0.0))};
    sig.has_dir = true;
  }
  if (f.params.find("edge_n1_x") != f.params.end()) {
    sig.normal1 = Vec3{static_cast<float>(get("edge_n1_x", 0.0)),
                       static_cast<float>(get("edge_n1_y", 0.0)),
                       static_cast<float>(get("edge_n1_z", 0.0))};
    sig.has_normal1 = true;
  }
  if (f.params.find("edge_n2_x") != f.params.end()) {
    sig.normal2 = Vec3{static_cast<float>(get("edge_n2_x", 0.0)),
                       static_cast<float>(get("edge_n2_y", 0.0)),
                       static_cast<float>(get("edge_n2_z", 0.0))};
    sig.has_normal2 = true;
  }
  return sig;
}

EdgeFingerprint fingerprint_from_signature(const Signature& sig) {
  EdgeFingerprint fp;
  fp.mid = sig.mid;
  fp.dir = sig.dir;
  fp.has_dir = sig.has_dir;
  fp.length = sig.length;
  fp.normal1 = sig.normal1;
  fp.has_normal1 = sig.has_normal1;
  fp.normal2 = sig.normal2;
  fp.has_normal2 = sig.has_normal2;
  return fp;
}

double normal_cost(bool has_a, Vec3 a, bool has_b, Vec3 b) {
  if (!has_a) {
    return 0.0;  // 采集时就没取到法线，不拿它当依据
  }
  if (!has_b) {
    return 0.1;  // 候选边取不到法线：轻微扣分，别让它白捡
  }
  const double d = std::min(static_cast<double>(std::fabs(dot(a, b))), 1.0);
  return 1.0 - d;
}

// 两个签名的加权代价：0 = 一模一样。位置和方向权重最高。
double signature_cost(const Signature& a, const Signature& b) {
  if (!a.valid || !b.valid) {
    return std::numeric_limits<double>::infinity();
  }
  const double dx = static_cast<double>(a.mid.x - b.mid.x);
  const double dy = static_cast<double>(a.mid.y - b.mid.y);
  const double dz = static_cast<double>(a.mid.z - b.mid.z);
  double cost = 2.0 * std::sqrt(dx * dx + dy * dy + dz * dz);
  if (a.has_dir && b.has_dir) {
    const double d = std::min(static_cast<double>(std::fabs(dot(a.dir, b.dir))), 1.0);
    cost += 2.0 * (1.0 - d);
  } else {
    cost += 0.25;
  }
  cost += 0.5 * std::fabs(a.length - b.length);
  cost += normal_cost(a.has_normal1, a.normal1, b.has_normal1, b.normal1);
  cost += normal_cost(a.has_normal2, a.normal2, b.has_normal2, b.normal2);
  return cost;
}

}  // namespace

void write_edge_fingerprint(std::unordered_map<std::string, double>& params,
                            const EdgeFingerprint& fp) {
  params["edge_fp_version"] = kFingerprintVersion;
  params["edge_mid_x"] = fp.mid.x;
  params["edge_mid_y"] = fp.mid.y;
  params["edge_mid_z"] = fp.mid.z;
  params["edge_len"] = fp.length;

  if (fp.has_dir) {
    params["edge_dir_x"] = fp.dir.x;
    params["edge_dir_y"] = fp.dir.y;
    params["edge_dir_z"] = fp.dir.z;
  } else {
    params.erase("edge_dir_x");
    params.erase("edge_dir_y");
    params.erase("edge_dir_z");
  }

  if (fp.has_normal1) {
    params["edge_n1_x"] = fp.normal1.x;
    params["edge_n1_y"] = fp.normal1.y;
    params["edge_n1_z"] = fp.normal1.z;
  } else {
    params.erase("edge_n1_x");
    params.erase("edge_n1_y");
    params.erase("edge_n1_z");
  }

  if (fp.has_normal2) {
    params["edge_n2_x"] = fp.normal2.x;
    params["edge_n2_y"] = fp.normal2.y;
    params["edge_n2_z"] = fp.normal2.z;
  } else {
    params.erase("edge_n2_x");
    params.erase("edge_n2_y");
    params.erase("edge_n2_z");
  }
}

void erase_edge_fingerprint(std::unordered_map<std::string, double>& params) {
  for (const char* key : kFingerprintKeys) {
    params.erase(key);
  }
}

bool has_edge_fingerprint(const std::unordered_map<std::string, double>& params) {
  const auto it = params.find("edge_fp_version");
  return it != params.end() && it->second == kFingerprintVersion;
}

bool is_edge_fingerprint_key(const std::string& key) {
  for (const char* candidate : kFingerprintKeys) {
    if (key == candidate) {
      return true;
    }
  }
  return false;
}

Result<EdgeFingerprint> capture_edge_fingerprint(const FeatureModel& model, ModelKernel& kernel,
                                                 std::uint64_t shape_feature_id,
                                                 int edge_index) {
  std::uint64_t target = shape_feature_id;
  if (target == 0) {
    const Feature* out = model.output_feature();
    if (out == nullptr) {
      return Err("edge fingerprint: feature model is empty");
    }
    target = out->id;
  }
  auto bodies = evaluate_feature_bodies(model, kernel, target);
  if (!bodies) {
    return Err(bodies.error());
  }
  const auto it = bodies->find(target);
  if (it == bodies->end()) {
    return Err("edge fingerprint: shape feature not found");
  }
  auto measures = kernel.measure_edges(*it->second);
  if (!measures) {
    return Err(measures.error());
  }
  if (edge_index < 0 || edge_index >= static_cast<int>(measures->size())) {
    return Err("edge fingerprint: edge index out of range");
  }
  auto bounds = kernel.bounds(*it->second);
  if (!bounds) {
    return Err(bounds.error());
  }
  return fingerprint_from_signature(
      signature_from_measure((*measures)[static_cast<std::size_t>(edge_index)], *bounds));
}

Result<EdgeFingerprint> capture_edge_fingerprint(const FeatureModel& model,
                                                 std::uint64_t shape_feature_id,
                                                 int edge_index) {
  auto kernel = default_kernel();
  if (!kernel) {
    return Err(kernel.error());
  }
  return capture_edge_fingerprint(model, **kernel, shape_feature_id, edge_index);
}

Result<EdgeId> resolve_edge(ModelKernel& kernel, const Body& body, const Feature& feature) {
  const std::string what = feature_kind_name(feature.kind);
  auto measures = kernel.measure_edges(body);
  if (!measures) {
    return Err(what + ": " + measures.error());
  }
  if (measures->empty()) {
    return Err(what + ": shape has no edges");
  }
  const auto edge_index_it = feature.params.find("edge");
  const int index =
      static_cast<int>(edge_index_it != feature.params.end() ? edge_index_it->second : 0.0);

  if (!has_edge_fingerprint(feature.params)) {
    if (index < 0 || index >= static_cast<int>(measures->size())) {
      return Err(what + ": edge index out of range");
    }
    return static_cast<EdgeId>(index);
  }

  auto bounds = kernel.bounds(body);
  if (!bounds) {
    return Err(what + ": " + bounds.error());
  }
  const Signature stored = signature_from_params(feature);
  std::vector<Signature> candidates;
  candidates.reserve(measures->size());
  for (const EdgeMeasure& measure : *measures) {
    candidates.push_back(signature_from_measure(measure, *bounds));
  }
  // 同一条几何边可能被枚举多次（每个面出现一次）：它们 key 相同、指纹完全一样。
  const auto same_edge_as_best = [&](int i, int best) {
    const auto a = static_cast<std::size_t>(i);
    const auto b = static_cast<std::size_t>(best);
    return (*measures)[a].key != 0 && (*measures)[a].key == (*measures)[b].key;
  };

  const bool index_in_range = index >= 0 && index < static_cast<int>(candidates.size());
  const double index_cost =
      index_in_range ? signature_cost(stored, candidates[static_cast<std::size_t>(index)])
                     : std::numeric_limits<double>::infinity();
  if (index_in_range && index_cost <= kEdgeMatchCost) {
    return static_cast<EdgeId>(index);
  }

  int best = -1;
  double best_cost = std::numeric_limits<double>::infinity();
  for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
    const double cost = (index_in_range && i == index)
                            ? index_cost
                            : signature_cost(stored, candidates[static_cast<std::size_t>(i)]);
    if (cost < best_cost) {
      best_cost = cost;
      best = i;
    }
  }
  if (best < 0 || best_cost > kEdgeMatchCost) {
    return Err(what + ": edge " + std::to_string(index) +
               " cannot be located after the model changed (geometry no longer matches)");
  }

  // 唯一性检查：和最优一样像的另一条边会让结果不可信。
  double second_cost = std::numeric_limits<double>::infinity();
  for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
    if (i == best || same_edge_as_best(i, best)) {
      continue;
    }
    const double cost = (index_in_range && i == index)
                            ? index_cost
                            : signature_cost(stored, candidates[static_cast<std::size_t>(i)]);
    second_cost = std::min(second_cost, cost);
  }
  if (index_in_range && index_cost <= best_cost + kEdgeMatchSlack) {
    return static_cast<EdgeId>(index);
  }
  if (second_cost - best_cost < kEdgeMatchMargin) {
    return Err(what + ": edge " + std::to_string(index) +
               " is ambiguous after the model changed (two edges match equally well)");
  }
  return static_cast<EdgeId>(best);
}

void refresh_edge_fingerprint(FeatureModel& model, ModelKernel& kernel,
                              std::uint64_t feature_id) {
  Feature* feature = model.find(feature_id);
  if (feature == nullptr) {
    return;
  }
  if (feature->kind != FeatureKind::Fillet && feature->kind != FeatureKind::Chamfer) {
    return;
  }
  erase_edge_fingerprint(feature->params);
  if (feature->inputs.empty()) {
    return;
  }
  const auto it = feature->params.find("edge");
  const int edge_index = static_cast<int>(it != feature->params.end() ? it->second : 0.0);
  if (auto captured = capture_edge_fingerprint(model, kernel, feature->inputs[0], edge_index);
      captured) {
    write_edge_fingerprint(feature->params, *captured);
  }
}

void refresh_edge_fingerprint(FeatureModel& model, std::uint64_t feature_id) {
  auto kernel = default_kernel();
  if (!kernel) {
    return;
  }
  refresh_edge_fingerprint(model, **kernel, feature_id);
}

}  // namespace tamias
