#include "bim/host_update.h"

#include "bim/host_geometry.h"
#include "engine/core/log.h"
#include "engine/document/document.h"

#include <string>

namespace tamias {
namespace {

Result<void> remesh_entity(Document& document, Entity& entity) {
  auto mesh = entity.createGeom();
  if (!mesh) {
    return Err(mesh.error());
  }
  MeshAsset* asset = document.mesh(entity.mesh_asset_id);
  if (asset == nullptr) {
    return Err("host_update: mesh asset not found");
  }
  asset->cpu = std::move(*mesh);
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
  set_opening_thickness(*guest, wall.thickness);
  const OpeningSize opening = opening_size(*guest);

  align_placement(relation.placement, wall, opening);
  relation.valid = placement_is_valid(relation.placement, wall, opening);

  sync_transform(document, *guest, hosted_transform(*host, relation.placement));
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

Result<void> notify_entity_changed(Document& document, std::uint64_t entity_id) {
  auto deps = document.bim().dependents(entity_id);
  if (deps.empty()) {
    return {};
  }
  for (Relation* relation : deps) {
    if (auto r = reshape_hosted(document, *relation); !r) {
      return r;
    }
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
  document.recompute_scene();
  document.mark_dirty();
  return {};
}

}  // namespace tamias
