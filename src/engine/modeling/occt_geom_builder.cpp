#include "occt_geom_builder.h"

#if defined(TAMIAS_HAS_OCCT)

#include "engine/modeling/occt_feature.h"

namespace tamias {
namespace {

// OCCT 后端：特征树 → BRep → 三角网。复用现有求值器 evaluate_feature_model。
class OcctGeometryBuilder final : public IGeometryBuilder {
 public:
  [[nodiscard]] Result<MeshCpu> build(const FeatureModel& model,
                                      double deflection) const override {
    return evaluate_feature_model(model, deflection);
  }
};

}  // namespace

IGeometryBuilder& geometry_builder() {
  static OcctGeometryBuilder instance;
  return instance;
}

}  // namespace tamias

#endif  // TAMIAS_HAS_OCCT
