#include "engine/modeling/occt_geom_builder.h"

#include "engine/modeling/edge_fingerprint.h"

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

// 没有 OCCT 就没有 BRep，取不到边的几何指纹：退回纯索引行为（refresh 会清掉指纹）。
Result<EdgeFingerprint> capture_edge_fingerprint(const FeatureModel&, std::uint64_t, int) {
  return Err("edge fingerprint requires the OCCT geometry kernel");
}

}  // namespace tamias
