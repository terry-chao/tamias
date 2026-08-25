#pragma once

#include "bim/location.h"
#include "engine/core/result.h"
#include "engine/graphics/mesh.h"
#include "engine/math/math.h"
#include "engine/modeling/feature.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tamias {

enum class EntityKind : std::uint8_t {
  Wall = 0,
  Box = 1,
  Cylinder = 2,
  Beam = 3,
  Column = 4,
  Slab = 5,
  Door = 6,
  Window = 7,
  Line = 8,
  Polyline = 9,
  Circle = 10,
  Arc = 11,
  Bezier = 12,
  Rectangle = 13,
  BSpline = 14,
  Nurbs = 15,
};

// 领域实体基类：参数化对象，持有特征树（造型配方）+ 放置 + 网格引用。
// SceneNode 是内部 render/select 单元，实体语义包裹它（entity id == scene node id）。
// 造型信息（几何生成）委托 createGeom 抽象（IGeometryBuilder），本类保持内核无关。
class Entity {
 public:
  Entity() = default;
  virtual ~Entity() = default;
  Entity(const Entity& other)
      : kind_(other.kind_),
        id(other.id),
        name(other.name),
        model(other.model),
        mesh_asset_id(other.mesh_asset_id),
        material_id(other.material_id),
        location(other.location ? other.location->clone() : nullptr),
        local_transform(other.local_transform),
        grips(other.grips) {}
  Entity& operator=(const Entity& other) {
    if (this != &other) {
      kind_ = other.kind_;
      id = other.id;
      name = other.name;
      model = other.model;
      mesh_asset_id = other.mesh_asset_id;
      material_id = other.material_id;
      location = other.location ? other.location->clone() : nullptr;
      local_transform = other.local_transform;
      grips = other.grips;
    }
    return *this;
  }
  Entity(Entity&&) noexcept = default;
  Entity& operator=(Entity&&) noexcept = default;

  [[nodiscard]] EntityKind kind() const { return kind_; }
  [[nodiscard]] virtual bool is_family_entity() const { return false; }
  [[nodiscard]] virtual bool is_sketch_entity() const { return false; }
  [[nodiscard]] std::unique_ptr<Entity> clone() const;
  // Location 是 BIM 构件放置真源；该函数刷新造型参数与 Mat4 缓存。
  void sync_from_location(double storey_elevation);
  // 兼容移动/夹点命令：把新的刚体变换反写到现有 Location。
  void sync_location_from_transform(const Mat4& transform, double storey_elevation);
  // 造型：委托 createGeom 抽象（IGeometryBuilder），生成几何。
  [[nodiscard]] Result<MeshCpu> createGeom(double deflection = 0.05) const;

  EntityKind kind_ = EntityKind::Wall;
  std::uint64_t id = 0;                    // = scene node id（对象身份）
  std::string name;
  FeatureModel model;                      // 特征树（造型配方）
  std::uint64_t mesh_asset_id = 0;         // 评估结果喂给的网格资产
  std::uint64_t material_id = 0;           // 材质库引用（0 = 默认材质）
  std::unique_ptr<Location> location;       // 柱=点、墙=线、板=面；其他实体可为空
  Mat4 local_transform = Mat4::identity();  // 放置（墙=位置+朝向，盒子/圆柱=位置）
  std::vector<Vec3> grips;                 // 局部夹点，随文档持久化
};

// 按 kind 构造一个实体（供反序列化 / clone 用）。
std::unique_ptr<Entity> make_entity(EntityKind kind);

}  // namespace tamias
