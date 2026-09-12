#include "bim/host_update.h"

#include "bim/host_geometry.h"
#include "bim/wall_join.h"
#include "entity/door_entity.h"
#include "entity/opening_entity.h"
#include "engine/core/log.h"
#include "engine/document/document.h"
#include "engine/modeling/feature.h"

#include <string>

namespace tamias {
namespace {

Result<void> remesh_entity(Document& document, Entity& entity) {
  auto mesh = entity.createGeom();
  if (!mesh) {
    return Err(mesh.error());
  }
  if (!document.replace_entity_mesh(entity.id, std::move(*mesh))) {
    return Err("host_update: mesh asset not found");
  }
  return {};
}

void sync_transform(Document& document, Entity& entity, Mat4 local) {
  entity.local_transform = local;
  document.scene().set_transform(entity.id, local);
}

Result<void> reshape_hosted(Document& document, Relation& relation) {
  Entity* guest = document.entity(relation.from);
  Entity* host = document.entity(relation.to);
  if (guest == nullptr || host == nullptr) {
    return Err("host_update: missing guest or host");
  }
  if (!can_host_opening(*host, *guest)) {
    relation.valid = false;
    return Err("host_update: host cannot accept this opening");
  }

  const WallSize wall = wall_size(*host);
  const OpeningSize opening = opening_size(*guest);

  relation.placement.sill = opening_sill_height(*guest);
  align_placement(relation.placement, wall, opening);
  relation.placement.offset = 0.0;  // 开口居中穿墙
  if (guest->kind() == EntityKind::Door) {
    (void)set_door_handle_depth(*guest, wall.thickness + 0.08);
    set_door_handle_side(*guest, relation.placement.handle_side);
  }
  relation.valid = placement_is_valid(relation.placement, wall, opening);

  sync_transform(document, *guest, hosted_transform(*host, relation.placement));
  if (const SceneNode* host_node = document.scene().find(host->id)) {
    document.scene().set_parent(guest->id, host_node->parent);
  }
  if (auto r = remesh_entity(document, *guest); !r) {
    return r;
  }
  if (!relation.valid) {
    log_warn("host_update: opening " + std::to_string(guest->id) +
             " does not fit host " + std::to_string(host->id));
  }
  return {};
}

}  // namespace

void append_hosted_opening_cuts(FeatureModel& model, std::uint64_t& current, const Entity& host,
                                const std::vector<const Relation*>& openings,
                                const Document& document) {
  const WallSize wall = wall_size(host);
  for (const Relation* relation : openings) {
    if (relation == nullptr || !relation->valid) {
      continue;
    }
    const Entity* guest = document.entity(relation->from);
    if (guest == nullptr) {
      continue;
    }
    const OpeningSize opening = opening_size(*guest);
    // 切穿整面墙：厚度方向必须明显大于宿主墙，两端都要伸出墙面。
    const double cut_thickness = wall.thickness + 0.20;
    auto& profile = model.add_feature(
        FeatureKind::RectProfile, {},
        {{"width", cut_thickness}, {"height", opening.width}});
    auto& extrude =
        model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", opening.height}});
    auto& xform = model.add_feature(
        FeatureKind::Transform, {extrude.id},
        {{"tx", relation->placement.offset},
         {"ty", relation->placement.sill},
         {"tz", (relation->placement.along - 0.5) * wall.length}});
    auto& cut = model.add_feature(
        FeatureKind::Boolean, {current, xform.id},
        {{"operation", static_cast<double>(static_cast<std::uint8_t>(BooleanOp::Cut))}});
    current = cut.id;
  }
}

Result<void> remesh_host_openings(Document& document, std::uint64_t host_id) {
  Entity* host = document.entity(host_id);
  if (host == nullptr) {
    return {};
  }
  if (!is_wall_host(*host)) {
    return {};
  }
  // 墙的造型只有一条路径：墙-墙倒角（墙局部斜接）+ 全部有效开口切减。
  return remesh_wall(document, host_id);
}

Result<void> notify_entity_changed(Document& document, std::uint64_t entity_id) {
  auto deps = document.bim().dependents(entity_id);
  if (!deps.empty()) {
    for (Relation* relation : deps) {
      if (auto r = reshape_hosted(document, *relation); !r) {
        return r;
      }
    }
    if (auto r = remesh_host_openings(document, entity_id); !r) {
      return r;
    }
  }
  if (Relation* hosted = document.bim().host_of(entity_id)) {
    if (auto r = reshape_hosted(document, *hosted); !r) {
      return r;
    }
    if (auto r = remesh_host_openings(document, hosted->to); !r) {
      return r;
    }
  }
  if (deps.empty() && document.bim().host_of(entity_id) == nullptr) {
    return {};
  }
  document.recompute_scene();
  document.mark_dirty();
  return {};
}

Result<void> bind_opening_to_host(Document& document, std::uint64_t guest_id,
                                  std::uint64_t host_id, Vec3 world_point) {
  Entity* guest = document.entity(guest_id);
  Entity* host = document.entity(host_id);
  if (guest == nullptr || host == nullptr) {
    return Err("bind_opening_to_host: missing guest or host");
  }
  if (!can_host_opening(*host, *guest)) {
    return Err("bind_opening_to_host: host is not a wall");
  }

  document.bim().remove_involving(guest_id);
  Relation relation{};
  relation.kind = RelationKind::HostedOn;
  relation.from = guest_id;
  relation.to = host_id;
  relation.placement = placement_from_world(*host, *guest, world_point);
  Relation& stored = document.bim().add(std::move(relation));
  if (auto r = reshape_hosted(document, stored); !r) {
    return r;
  }
  if (auto r = remesh_host_openings(document, host_id); !r) {
    return r;
  }
  document.recompute_scene();
  document.mark_dirty();
  return {};
}

}  // namespace tamias
