#include "entity/door_entity.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tamias {

namespace {

constexpr double kRoseHeight = 0.018;
constexpr double kHandleOverlap = 0.01;

double feature_param(const Feature& feature, const char* name, double fallback = 0.0) {
  const auto it = feature.params.find(name);
  return it == feature.params.end() ? fallback : it->second;
}

bool set_feature_param(Feature& feature, const char* name, double value) {
  const auto it = feature.params.find(name);
  if (it != feature.params.end() && std::fabs(it->second - value) <= 1e-9) {
    return false;
  }
  feature.params[name] = value;
  return true;
}

const Feature* first_rect_profile(const FeatureModel& model) {
  for (const Feature& feature : model.features()) {
    if (feature.kind == FeatureKind::RectProfile) {
      return &feature;
    }
  }
  return nullptr;
}

const Feature* first_extrude(const FeatureModel& model) {
  for (const Feature& feature : model.features()) {
    if (feature.kind == FeatureKind::Extrude) {
      return &feature;
    }
  }
  return nullptr;
}

bool has_handle_cylinder(const FeatureModel& model) {
  for (const Feature& feature : model.features()) {
    if (feature.kind == FeatureKind::Cylinder &&
        feature.params.find("handle_part") != feature.params.end()) {
      return true;
    }
  }
  return false;
}

struct OldBlockHandle {
  std::uint64_t transform_id = 0;
  double side = 1.0;
  double depth = 0.0;
};

OldBlockHandle find_old_block_handle(const FeatureModel& model) {
  OldBlockHandle old{};
  for (const Feature& feature : model.features()) {
    if (feature.kind != FeatureKind::Transform ||
        feature.params.find("handle_side") == feature.params.end() ||
        feature.params.find("handle_offset") == feature.params.end()) {
      continue;
    }
    old.transform_id = feature.id;
    old.side = feature_param(feature, "handle_side", 1.0) < 0.0 ? -1.0 : 1.0;
    old.depth = feature_param(feature, "handle_depth", 0.0);
    break;
  }
  return old;
}

void remove_old_block_handle(FeatureModel& model, const OldBlockHandle& old) {
  if (old.transform_id == 0) {
    return;
  }

  std::vector<std::uint64_t> remove_ids{old.transform_id};
  if (const Feature* transform = model.find(old.transform_id)) {
    if (!transform->inputs.empty()) {
      const std::uint64_t extrude_id = transform->inputs[0];
      remove_ids.push_back(extrude_id);
      if (const Feature* extrude = model.find(extrude_id)) {
        if (!extrude->inputs.empty()) {
          remove_ids.push_back(extrude->inputs[0]);
        }
      }
    }
  }
  for (const Feature& feature : model.features()) {
    if (feature.kind != FeatureKind::Boolean) {
      continue;
    }
    for (std::uint64_t input : feature.inputs) {
      if (input == old.transform_id) {
        remove_ids.push_back(feature.id);
        break;
      }
    }
  }
  for (std::uint64_t id : remove_ids) {
    model.remove_feature(id);
  }
}

double default_handle_reach(double thickness) {
  return std::max(0.085, thickness * 0.5 + 0.06);
}

std::uint64_t add_handle_cylinder(FeatureModel& model, int part, double face, double side,
                                  double handle_offset, double lever_length, double reach,
                                  double leaf_half, double lever_radius, double radius,
                                  double height, Vec3 center, Vec3 axis) {
  std::unordered_map<std::string, double> params{
      {"radius", radius},
      {"height", height},
      {"cx", static_cast<double>(center.x)},
      {"cy", static_cast<double>(center.y)},
      {"cz", static_cast<double>(center.z)},
      {"ax", static_cast<double>(axis.x)},
      {"ay", static_cast<double>(axis.y)},
      {"az", static_cast<double>(axis.z)},
      {"handle_part", static_cast<double>(part)},
      {"handle_face", face},
      {"handle_side", side},
      {"handle_offset", handle_offset},
      {"lever_length", lever_length},
      {"handle_reach", reach},
      {"leaf_half", leaf_half},
      {"rose_height", kRoseHeight},
      {"lever_radius", lever_radius},
  };
  return model.add_feature(FeatureKind::Cylinder, {}, std::move(params)).id;
}

// 真实门把手的简化造型：门面圆盘底座 + 向外的连接颈 + 水平杠杆把手。
std::uint64_t build_door_handle(FeatureModel& model, std::uint64_t leaf_shape,
                                double width, double height, double thickness,
                                double side, double reach) {
  side = side < 0.0 ? -1.0 : 1.0;

  const double rose_radius = std::clamp(width * 0.035, 0.025, 0.045);
  const double neck_radius = rose_radius * 0.38;
  const double lever_radius = std::max(0.010, rose_radius * 0.34);
  const double lever_length = std::clamp(width * 0.16, 0.12, 0.22);
  const double margin = std::max(0.04, width * 0.05);
  const double handle_offset =
      std::max(rose_radius, width * 0.5 - rose_radius - margin);

  const double y_lo = rose_radius + 0.02;
  const double y_hi = std::max(y_lo, height - y_lo);
  const double handle_y = std::clamp(1.0, y_lo, y_hi);
  const double leaf_half = thickness * 0.5;
  const double min_reach = leaf_half + kRoseHeight + lever_radius + 0.02;
  reach = std::max(reach, min_reach);

  const double rose_center_z = leaf_half + kRoseHeight * 0.5 - kHandleOverlap;
  const double rose_outer_z = leaf_half + kRoseHeight - kHandleOverlap;
  const double neck_height =
      std::max(reach - rose_outer_z + 2.0 * kHandleOverlap, 0.03);
  const double neck_center_z = (rose_outer_z + reach) * 0.5;
  const double lever_center_x =
      side * std::max(0.0, handle_offset - lever_length * 0.5);

  std::uint64_t current = leaf_shape;
  auto fuse = [&](std::uint64_t cylinder_id) {
    current = model
                  .add_feature(FeatureKind::Boolean, {current, cylinder_id},
                               {{"operation", 0.0}, {"handle_fuse", 1.0}})
                  .id;
  };

  for (double face : {1.0, -1.0}) {
    const Vec3 rose_center{static_cast<float>(side * handle_offset),
                           static_cast<float>(handle_y),
                           static_cast<float>(face * rose_center_z)};
    const Vec3 neck_center{static_cast<float>(side * handle_offset),
                           static_cast<float>(handle_y),
                           static_cast<float>(face * neck_center_z)};
    const Vec3 lever_center{static_cast<float>(lever_center_x),
                            static_cast<float>(handle_y),
                            static_cast<float>(face * reach)};

    fuse(add_handle_cylinder(model, 1, face, side, handle_offset, lever_length, reach,
                             leaf_half, lever_radius, rose_radius, kRoseHeight,
                             rose_center, {0.f, 0.f, 1.f}));
    fuse(add_handle_cylinder(model, 2, face, side, handle_offset, lever_length, reach,
                             leaf_half, lever_radius, neck_radius, neck_height,
                             neck_center, {0.f, 0.f, 1.f}));
    fuse(add_handle_cylinder(model, 3, face, side, handle_offset, lever_length, reach,
                             leaf_half, lever_radius, lever_radius, lever_length,
                             lever_center, {1.f, 0.f, 0.f}));
  }
  return current;
}

}  // namespace

DoorEntity::DoorEntity(Vec3 position, double width, double height, double thickness,
                       double sill)
    : OpeningEntity(EntityKind::Door, "Single-Flush Door", 0.0) {
  name = "door";
  auto& profile =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", width}, {"height", thickness}});
  auto& leaf = model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  build_door_handle(model, leaf.id, width, height, thickness, 1.0,
                    default_handle_reach(thickness));
  set_sill_height(sill);
  local_transform = translate(position);
}

bool ensure_door_handle(Entity& entity) {
  if (entity.kind() != EntityKind::Door || has_handle_cylinder(entity.model)) {
    return false;
  }

  const OldBlockHandle old = find_old_block_handle(entity.model);
  if (old.transform_id != 0) {
    remove_old_block_handle(entity.model, old);
  }

  const Feature* leaf_profile = first_rect_profile(entity.model);
  const Feature* leaf_extrude = first_extrude(entity.model);
  if (leaf_profile == nullptr || leaf_extrude == nullptr) {
    return false;
  }

  const double width = entity.model.param(leaf_profile->id, "width", 1.0);
  const double thickness = entity.model.param(leaf_profile->id, "height", 0.05);
  const double height = entity.model.param(leaf_extrude->id, "depth", 2.1);
  const Feature* output = entity.model.output_feature();
  const std::uint64_t leaf_shape = output != nullptr ? output->id : leaf_extrude->id;
  const double reach =
      old.depth > 0.0 ? old.depth * 0.5 : default_handle_reach(thickness);

  build_door_handle(entity.model, leaf_shape, width, height, thickness, old.side, reach);
  return true;
}

double door_handle_side(const Entity& entity) {
  for (const Feature& feature : entity.model.features()) {
    if (feature.kind != FeatureKind::Cylinder ||
        feature.params.find("handle_part") == feature.params.end()) {
      continue;
    }
    return feature_param(feature, "handle_side", 1.0) < 0.0 ? -1.0 : 1.0;
  }
  return 1.0;
}

void set_door_handle_side(Entity& entity, double side) {
  if (entity.kind() != EntityKind::Door) {
    return;
  }
  (void)ensure_door_handle(entity);

  const double normalized = side < 0.0 ? -1.0 : 1.0;
  for (Feature& feature : entity.model.features()) {
    if (feature.kind != FeatureKind::Cylinder ||
        feature.params.find("handle_part") == feature.params.end()) {
      continue;
    }
    const int part = static_cast<int>(feature_param(feature, "handle_part", 0.0));
    const double offset = feature_param(feature, "handle_offset", 0.0);
    const double lever_length = feature_param(feature, "lever_length", 0.0);
    (void)set_feature_param(feature, "handle_side", normalized);
    if (part == 3) {
      (void)set_feature_param(
          feature, "cx", normalized * std::max(0.0, offset - lever_length * 0.5));
      (void)set_feature_param(feature, "ax", 1.0);
      (void)set_feature_param(feature, "ay", 0.0);
      (void)set_feature_param(feature, "az", 0.0);
    } else {
      (void)set_feature_param(feature, "cx", normalized * offset);
    }
  }
}

bool set_door_handle_depth(Entity& entity, double depth) {
  if (entity.kind() != EntityKind::Door) {
    return false;
  }

  bool changed = ensure_door_handle(entity);
  const Feature* leaf_profile = first_rect_profile(entity.model);
  if (leaf_profile == nullptr) {
    return changed;
  }
  const double thickness = entity.model.param(leaf_profile->id, "height", 0.05);
  const double leaf_half = thickness * 0.5;
  const double next_depth = std::max(depth, 0.02);

  for (Feature& feature : entity.model.features()) {
    if (feature.kind != FeatureKind::Cylinder ||
        feature.params.find("handle_part") == feature.params.end()) {
      continue;
    }
    const int part = static_cast<int>(feature_param(feature, "handle_part", 0.0));
    const double face = feature_param(feature, "handle_face", 1.0);
    const double rose_height = feature_param(feature, "rose_height", kRoseHeight);
    const double lever_radius = feature_param(feature, "lever_radius", 0.012);
    const double reach =
        std::max(next_depth * 0.5, leaf_half + rose_height + lever_radius + 0.02);

    changed |= set_feature_param(feature, "handle_reach", reach);
    changed |= set_feature_param(feature, "leaf_half", leaf_half);
    if (part == 1) {
      changed |= set_feature_param(
          feature, "cz", face * (leaf_half + rose_height * 0.5 - kHandleOverlap));
      changed |= set_feature_param(feature, "height", rose_height);
    } else if (part == 2) {
      const double rose_outer = leaf_half + rose_height - kHandleOverlap;
      const double neck_height =
          std::max(reach - rose_outer + 2.0 * kHandleOverlap, 0.03);
      changed |= set_feature_param(feature, "cz", face * ((rose_outer + reach) * 0.5));
      changed |= set_feature_param(feature, "height", neck_height);
    } else if (part == 3) {
      changed |= set_feature_param(feature, "cz", face * reach);
    }
  }
  return changed;
}

}  // namespace tamias
