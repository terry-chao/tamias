#include "engine/modeling/evaluate/geom_builder.h"

#include "engine/modeling/evaluate/evaluator.h"

namespace tamias {
namespace {

// 过渡实现：转给默认内核求值。没有内核（例如 WASM 阶段 1 不带 OCCT）时，
// evaluate_feature_model 会返回「kernel not registered」，调用方照旧看到错误。
class DefaultGeometryBuilder final : public IGeometryBuilder {
 public:
  [[nodiscard]] Result<MeshCpu> build(const FeatureModel& model,
                                      double deflection) const override {
    return evaluate_feature_model(model, deflection);
  }
};

}  // namespace

IGeometryBuilder& geometry_builder() {
  static DefaultGeometryBuilder instance;
  return instance;
}

}  // namespace tamias
