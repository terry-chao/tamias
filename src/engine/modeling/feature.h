#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tamias {

// 参数化几何的「特征」= 一个带参数、可引用上游结果的操作步骤。
enum class FeatureKind : std::uint8_t {
  RectProfile = 0,  // 矩形轮廓面，params{ width, height }
  Extrude = 1,      // 拉伸，input[0] = 轮廓 feature id，params{ depth }
};

struct Feature {
  std::uint64_t id = 0;
  FeatureKind kind = FeatureKind::RectProfile;
  std::vector<std::uint64_t> inputs;              // 依赖的 feature id
  std::unordered_map<std::string, double> params; // 命名参数
};

// 特征树：一串带参数的步骤，求值得到几何。
// 数据模型与几何内核无关（可序列化）；求值器在 OCCT 后端（见 occt_feature.h）。
class FeatureModel {
 public:
  Feature& add_feature(FeatureKind kind, std::vector<std::uint64_t> inputs,
                       std::unordered_map<std::string, double> params) {
    Feature f;
    f.id = next_id_++;
    f.kind = kind;
    f.inputs = std::move(inputs);
    f.params = std::move(params);
    features_.push_back(std::move(f));
    return features_.back();
  }

  Feature* find(std::uint64_t id) {
    for (auto& f : features_) {
      if (f.id == id) {
        return &f;
      }
    }
    return nullptr;
  }

  const Feature* find(std::uint64_t id) const {
    for (const auto& f : features_) {
      if (f.id == id) {
        return &f;
      }
    }
    return nullptr;
  }

  void set_param(std::uint64_t id, const std::string& name, double value) {
    if (Feature* f = find(id)) {
      f->params[name] = value;
    }
  }

  [[nodiscard]] double param(std::uint64_t id, const std::string& name,
                             double fallback = 0.0) const {
    const Feature* f = find(id);
    if (f == nullptr) {
      return fallback;
    }
    const auto it = f->params.find(name);
    return it == f->params.end() ? fallback : it->second;
  }

  [[nodiscard]] const std::vector<Feature>& features() const { return features_; }
  [[nodiscard]] std::vector<Feature>& features() { return features_; }

  // 输出特征 = 不被任何其他特征引用的那个（树根）。单链时即最后一个。
  [[nodiscard]] const Feature* output_feature() const {
    if (features_.empty()) {
      return nullptr;
    }
    for (const auto& f : features_) {
      bool referenced = false;
      for (const auto& other : features_) {
        for (std::uint64_t in : other.inputs) {
          if (in == f.id) {
            referenced = true;
            break;
          }
        }
        if (referenced) {
          break;
        }
      }
      if (!referenced) {
        return &f;
      }
    }
    return &features_.back();
  }

 private:
  std::vector<Feature> features_;
  std::uint64_t next_id_ = 1;
};

}  // namespace tamias
