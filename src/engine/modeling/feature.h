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
  CircleProfile = 2,  // 圆形轮廓面，params{ radius }
  Boolean = 3,      // 布尔运算，inputs[0..1] = 两个 shape，params{ operation }
  Fillet = 4,       // 倒圆角，inputs[0] = shape，params{ radius, edge }
  Chamfer = 5,      // 倒斜角，inputs[0] = shape，params{ distance, edge }
  Line = 6,         // 直线，params{ ax,ay,az, bx,by,bz }
  Polyline = 7,     // 折线，params{ n, p0x,p0y,p0z, ... }
  CircleWire = 8,   // 圆曲线（非轮廓面），params{ cx,cy,cz, radius }
  Arc = 9,          // 三点圆弧 start/through/end，params{ ax..cz }
  Bezier = 10,      // 贝塞尔，params{ n, p0x,p0y,p0z, ... }；旧文件可能只有 p0..p3
  RectWire = 11,    // 轴对齐矩形轮廓，params{ ax,ay,az, bx,by,bz }
  BSpline = 12,     // 夹紧均匀 B 样条，params{ n, degree, p0x,p0y,p0z, ... }
  Nurbs = 13,       // NURBS，params{ n, degree, p0x.., w0, w1, ... }
};

inline bool is_sketch_feature(FeatureKind kind) {
  switch (kind) {
    case FeatureKind::Line:
    case FeatureKind::Polyline:
    case FeatureKind::CircleWire:
    case FeatureKind::Arc:
    case FeatureKind::Bezier:
    case FeatureKind::RectWire:
    case FeatureKind::BSpline:
    case FeatureKind::Nurbs:
      return true;
    default:
      return false;
  }
}

// 布尔运算类型（存成 Boolean 特征的 operation 参数）。
enum class BooleanOp : std::uint8_t { Fuse = 0, Common = 1, Cut = 2 };

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

  // 删除特征。不修 dangling 引用——只应删「树根/输出」这类不被引用的特征（undo 用）。
  void remove_feature(std::uint64_t id) {
    for (auto it = features_.begin(); it != features_.end(); ++it) {
      if (it->id == id) {
        features_.erase(it);
        return;
      }
    }
  }

  // 把 another 的特征追加到本树末尾，重映射其 id 与 inputs 引用。要求 another 的特征按
  // 拓扑序排列（依赖在前，add_feature 的插入顺序即如此）。返回 another 旧 id → 新 id。
  std::unordered_map<std::uint64_t, std::uint64_t> append(const FeatureModel& another) {
    std::unordered_map<std::uint64_t, std::uint64_t> remap;
    for (const Feature& f : another.features()) {
      Feature nf = f;
      const std::uint64_t old_id = nf.id;
      nf.id = next_id_++;
      for (std::uint64_t& in : nf.inputs) {
        in = remap.at(in);
      }
      remap[old_id] = nf.id;
      features_.push_back(std::move(nf));
    }
    return remap;
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
