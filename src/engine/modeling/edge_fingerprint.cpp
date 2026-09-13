#include "engine/modeling/edge_fingerprint.h"

#include <array>

namespace tamias {
namespace {

// 指纹占用的参数键。write / erase / 属性面板过滤都以这张表为准。
constexpr std::array<const char*, 13> kFingerprintKeys = {
    "edge_mid_x", "edge_mid_y", "edge_mid_z", "edge_dir_x", "edge_dir_y", "edge_dir_z",
    "edge_len",   "edge_n1_x",  "edge_n1_y",  "edge_n1_z",  "edge_n2_x",  "edge_n2_y",
    "edge_n2_z"};

}  // namespace

void write_edge_fingerprint(std::unordered_map<std::string, double>& params,
                            const EdgeFingerprint& fp) {
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
  return params.find("edge_mid_x") != params.end();
}

bool is_edge_fingerprint_key(const std::string& key) {
  for (const char* candidate : kFingerprintKeys) {
    if (key == candidate) {
      return true;
    }
  }
  return false;
}

void refresh_edge_fingerprint(FeatureModel& model, std::uint64_t feature_id) {
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
  if (auto captured = capture_edge_fingerprint(model, feature->inputs[0], edge_index);
      captured) {
    write_edge_fingerprint(feature->params, *captured);
  }
}

}  // namespace tamias
