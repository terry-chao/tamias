#include "engine/modeling/occt_geom_builder.h"

namespace tamias {
namespace {

class StubGeometryBuilder final : public IGeometryBuilder {
 public:
  [[nodiscard]] Result<MeshCpu> build(const FeatureModel&, double) const override {
    return Err("OCCT geometry builder is not linked in this build");
  }
};

}  // namespace

IGeometryBuilder& geometry_builder() {
  static StubGeometryBuilder instance;
  return instance;
}

}  // namespace tamias
