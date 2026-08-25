#include "entity/slab_entity.h"

#include "bim/surface_location.h"

#include <algorithm>
#include <cmath>

namespace tamias {
namespace {

void init_slab(SlabEntity& slab, Vec3 center, double length, double width, double thickness) {
  slab.name = "slab";
  auto& profile = slab.model.add_feature(FeatureKind::RectProfile, {},
                                         {{"width", length}, {"height", width}});
  slab.model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", thickness}});
  slab.location = std::make_unique<SurfaceLocation>(
      Vec3{center.x, 0.f, center.z}, Vec3{1.f, 0.f, 0.f}, 0,
      static_cast<double>(center.y));
  slab.sync_from_location(0.0);
}

}  // namespace

SlabEntity::SlabEntity(Vec3 position, double length, double width, double thickness)
    : FamilyEntity(EntityKind::Slab, "Concrete Slab") {
  init_slab(*this, position, length, width, thickness);
}

SlabEntity::SlabEntity(Vec3 a, Vec3 b, double thickness)
    : FamilyEntity(EntityKind::Slab, "Concrete Slab") {
  const double length = static_cast<double>(std::max(std::abs(b.x - a.x), 1e-3f));
  const double width = static_cast<double>(std::max(std::abs(b.z - a.z), 1e-3f));
  const Vec3 mid{(a.x + b.x) * 0.5f, a.y, (a.z + b.z) * 0.5f};
  init_slab(*this, mid, length, width, thickness);
}

}  // namespace tamias
