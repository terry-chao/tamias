#include "bim/host_geometry.h"
#include "bim/host_update.h"
#include "bim/wall_join.h"
#include "command/move_entities_command.h"
#include "engine/document/document.h"
#include "engine/graphics/mesh.h"
#include "engine/modeling/occt_feature.h"
#include "entity/wall_entity.h"
#include "entity/window_entity.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace tamias {
namespace {

// 建一面墙并放进文档（和 CreateWallCommand 同一条路径）。
Entity* add_wall(Document& document, Vec3 start, Vec3 end, double thickness = 0.2,
                 double height = 3.0) {
  WallEntity wall(start, end, thickness, height);
  document.assign_active_storey(wall);
  auto geometry = wall.createGeom();
  if (!geometry) {
    return nullptr;
  }
  return document.add_entity(std::make_unique<WallEntity>(std::move(wall)), std::move(*geometry));
}

// 墙的平面轮廓（已变换到世界）。
std::vector<Vec3> world_footprint(const Document& document, std::uint64_t wall_id) {
  std::vector<Vec3> points;
  const Entity* wall = document.entity(wall_id);
  if (wall == nullptr) {
    return points;
  }
  points = wall_junction_footprint(wall_size(*wall), find_wall_junctions(*wall, document));
  for (Vec3& p : points) {
    p = wall->local_transform * p;
  }
  return points;
}

bool inside_plan(const std::vector<Vec3>& polygon, Vec3 point) {
  const std::size_t count = polygon.size();
  if (count < 3) {
    return false;
  }
  float sign = 0.f;
  for (std::size_t i = 0; i < count; ++i) {
    const Vec3 a = polygon[i];
    const Vec3 b = polygon[(i + 1) % count];
    const float cross = (b.x - a.x) * (point.z - a.z) - (b.z - a.z) * (point.x - a.x);
    if (std::fabs(cross) < 1e-6f) {
      continue;
    }
    const float s = cross > 0.f ? 1.f : -1.f;
    if (sign == 0.f) {
      sign = s;
    } else if (s != sign) {
      return false;
    }
  }
  return sign != 0.f;
}

// 沿 +Y 打射线数交点（奇偶）：probe 是否落在该构件的实体里。用来验证网格真的
// 补上了外角，而不只是包围盒变大。
bool mesh_covers(const Document& document, std::uint64_t entity_id, Vec3 probe) {
  const Entity* entity = document.entity(entity_id);
  if (entity == nullptr) {
    return false;
  }
  const MeshAsset* asset = document.mesh(entity->mesh_asset_id);
  if (asset == nullptr) {
    return false;
  }
  const MeshCpu& mesh = asset->cpu;
  int crossings = 0;
  for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const Vec3 a = entity->local_transform * mesh.vertices[mesh.indices[i]].position;
    const Vec3 b = entity->local_transform * mesh.vertices[mesh.indices[i + 1]].position;
    const Vec3 c = entity->local_transform * mesh.vertices[mesh.indices[i + 2]].position;
    const float denominator = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
    if (std::fabs(denominator) < 1e-9f) {
      continue;
    }
    const float l1 =
        ((b.z - c.z) * (probe.x - c.x) + (c.x - b.x) * (probe.z - c.z)) / denominator;
    const float l2 =
        ((c.z - a.z) * (probe.x - c.x) + (a.x - c.x) * (probe.z - c.z)) / denominator;
    const float l3 = 1.f - l1 - l2;
    const float eps = 1e-5f;
    if (l1 < -eps || l2 < -eps || l3 < -eps || l1 > 1.f + eps || l2 > 1.f + eps ||
        l3 > 1.f + eps) {
      continue;
    }
    if (l1 * a.y + l2 * b.y + l3 * c.y > probe.y) {
      ++crossings;
    }
  }
  return (crossings % 2) == 1;
}

// 有向体积：三角形绕向朝外时为正。用来抓「轮廓绕向反了 → 法线朝内」。
float signed_volume(const MeshCpu& mesh) {
  float volume = 0.f;
  for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const Vec3 a = mesh.vertices[mesh.indices[i]].position;
    const Vec3 b = mesh.vertices[mesh.indices[i + 1]].position;
    const Vec3 c = mesh.vertices[mesh.indices[i + 2]].position;
    volume += dot(a, cross(b, c)) / 6.f;
  }
  return volume;
}

// 两面墙成 L 形：一面沿 +X 到 (4,0)，一面从 (4,0) 沿 +Z 走。
struct CornerWalls {
  Document document;
  std::uint64_t a = 0;
  std::uint64_t b = 0;
};

CornerWalls make_corner(double thickness = 0.2) {
  CornerWalls walls;
  Entity* a = add_wall(walls.document, {0.f, 0.f, 0.f}, {4.f, 0.f, 0.f}, thickness);
  Entity* b = add_wall(walls.document, {4.f, 0.f, 0.f}, {4.f, 0.f, 4.f}, thickness);
  if (a != nullptr) {
    walls.a = a->id;
  }
  if (b != nullptr) {
    walls.b = b->id;
  }
  return walls;
}

TEST(WallJunction, CornerFootprintFillsOuterSquare) {
  CornerWalls walls = make_corner();
  ASSERT_NE(walls.a, 0u);
  ASSERT_NE(walls.b, 0u);

  const std::vector<Vec3> fa = world_footprint(walls.document, walls.a);
  const std::vector<Vec3> fb = world_footprint(walls.document, walls.b);
  ASSERT_GE(fa.size(), 3u);
  ASSERT_GE(fb.size(), 3u);

  const auto covered = [&](Vec3 p) { return inside_plan(fa, p) || inside_plan(fb, p); };
  // 两段长方体硬拼时，外角方块（x>4、z<0 那一块）是空的——斜面之后必须被补上。
  EXPECT_TRUE(covered({4.03f, 0.f, -0.06f}));
  EXPECT_TRUE(covered({4.06f, 0.f, -0.03f}));
  EXPECT_TRUE(covered({3.97f, 0.f, 0.06f}));
  // 外角之外、墙侧之外仍然空着。
  EXPECT_FALSE(covered({4.2f, 0.f, -0.2f}));
  EXPECT_FALSE(covered({4.2f, 0.f, 0.2f}));
  EXPECT_FALSE(covered({3.8f, 0.f, -0.2f}));

  // 斜接面过接点 (4, 0)、方向 (1,-1)：两墙各自停在面的一侧（只留咬入余量）。
  const float eps = 0.005f;
  for (const Vec3& p : fa) {
    EXPECT_LE(p.x - 4.f + p.z, eps);
  }
  for (const Vec3& p : fb) {
    EXPECT_GE(p.x - 4.f + p.z, -eps);
  }
}

TEST(WallJunction, CornerMeshesCoverCornerIn3d) {
  CornerWalls walls = make_corner();
  ASSERT_NE(walls.a, 0u);
  ASSERT_NE(walls.b, 0u);

  const Vec3 near_outer_corner{4.03f, 1.5f, -0.06f};
  const Vec3 beyond_corner{4.2f, 1.5f, -0.2f};
  EXPECT_TRUE(mesh_covers(walls.document, walls.a, near_outer_corner) ||
              mesh_covers(walls.document, walls.b, near_outer_corner));
  EXPECT_FALSE(mesh_covers(walls.document, walls.a, beyond_corner));
  EXPECT_FALSE(mesh_covers(walls.document, walls.b, beyond_corner));
  EXPECT_FALSE(mesh_covers(walls.document, walls.a, {2.f, 1.5f, -0.2f}));
}

TEST(WallJunction, CornerMeshStaysOutsideInwardFacing) {
  CornerWalls walls = make_corner();
  ASSERT_NE(walls.a, 0u);

  // 斜接后墙体仍然实心：墙中段内部一点在实体内，墙外侧一点不在。
  EXPECT_TRUE(mesh_covers(walls.document, walls.a, {2.f, 1.5f, 0.f}));
  EXPECT_FALSE(mesh_covers(walls.document, walls.a, {2.f, 1.5f, 0.3f}));
  EXPECT_FALSE(mesh_covers(walls.document, walls.a, {2.f, 3.5f, 0.f}));
}

TEST(WallJunction, JoinedMeshKeepsOutwardWinding) {
  // 没交接的墙（矩形轮廓）和斜接后的墙（多边形轮廓）都必须是外向绕向：
  // 轮廓点写反会让整面墙法线朝内，光照和背面剔除都会错。
  WallEntity plain({0.f, 0.f, 0.f}, {4.f, 0.f, 0.f}, 0.2, 3.0);
  auto plain_mesh = plain.createGeom();
  ASSERT_TRUE(plain_mesh) << plain_mesh.error();
  EXPECT_GT(signed_volume(*plain_mesh), 0.f);

  CornerWalls walls = make_corner();
  ASSERT_NE(walls.a, 0u);
  ASSERT_NE(walls.b, 0u);
  for (const std::uint64_t id : {walls.a, walls.b}) {
    const Entity* wall = walls.document.entity(id);
    ASSERT_NE(wall, nullptr);
    const MeshAsset* asset = walls.document.mesh(wall->mesh_asset_id);
    ASSERT_NE(asset, nullptr);
    EXPECT_GT(signed_volume(asset->cpu), 0.f);
  }
}

TEST(WallJunction, TJunctionStopsAtNeighborFace) {
  Document document;
  Entity* through = add_wall(document, {0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, 0.3);
  ASSERT_NE(through, nullptr);
  Entity* stub = add_wall(document, {5.f, 0.f, 5.f}, {5.f, 0.f, 0.f}, 0.2);
  ASSERT_NE(stub, nullptr);

  // 顶在邻墙中心线上的墙，裁到邻墙近侧墙面（z = +0.15），只留咬入余量。
  const std::vector<Vec3> stub_footprint = world_footprint(document, stub->id);
  ASSERT_GE(stub_footprint.size(), 3u);
  float min_z = 1e9f;
  for (const Vec3& p : stub_footprint) {
    min_z = std::min(min_z, p.z);
    EXPECT_GE(p.z, 0.15f - 0.005f);
  }
  EXPECT_LE(min_z, 0.1501f);
  EXPECT_GE(min_z, 0.148f);

  // 被顶的墙自己不受影响：还是完整矩形，厚度方向 ±0.15。
  const std::vector<Vec3> through_footprint = world_footprint(document, through->id);
  EXPECT_EQ(through_footprint.size(), 4u);
  for (const Vec3& p : through_footprint) {
    EXPECT_LE(std::fabs(p.z), 0.1501f);
  }
}

TEST(WallJunction, CollinearWallsKeepFlatEnds) {
  Document document;
  Entity* first = add_wall(document, {0.f, 0.f, 0.f}, {4.f, 0.f, 0.f});
  ASSERT_NE(first, nullptr);
  Entity* second = add_wall(document, {4.f, 0.f, 0.f}, {8.f, 0.f, 0.f});
  ASSERT_NE(second, nullptr);

  // 一直线接下去：两端都不用斜切，只留咬入余量。
  const std::vector<Vec3> fa = world_footprint(document, first->id);
  const std::vector<Vec3> fb = world_footprint(document, second->id);
  EXPECT_EQ(fa.size(), 4u);
  EXPECT_EQ(fb.size(), 4u);
  for (const Vec3& p : fa) {
    EXPECT_LE(p.x, 4.f + 0.005f);
  }
  for (const Vec3& p : fb) {
    EXPECT_GE(p.x, 4.f - 0.005f);
  }
}

TEST(WallJunction, DifferentElevationsDoNotJoin) {
  Document document;
  Entity* ground = add_wall(document, {0.f, 0.f, 0.f}, {4.f, 0.f, 0.f});
  ASSERT_NE(ground, nullptr);
  const std::uint64_t storey_id = document.add_storey("2F", 3.0).id;
  document.set_active_storey(storey_id);
  Entity* upper = add_wall(document, {4.f, 3.f, 0.f}, {4.f, 3.f, 4.f});
  ASSERT_NE(upper, nullptr);

  // 端点平面位置一样，但不在同一层：不该互相斜接。
  EXPECT_TRUE(find_wall_junctions(*document.entity(ground->id), document).empty());
  EXPECT_TRUE(find_wall_junctions(*document.entity(upper->id), document).empty());
  EXPECT_EQ(world_footprint(document, ground->id).size(), 4u);
  EXPECT_EQ(world_footprint(document, upper->id).size(), 4u);
}

TEST(WallJunction, MovingWallNextToNeighborCreatesJoint) {
  Document document;
  Entity* a = add_wall(document, {0.f, 0.f, 0.f}, {4.f, 0.f, 0.f});
  ASSERT_NE(a, nullptr);
  Entity* b = add_wall(document, {10.f, 0.f, 0.f}, {10.f, 0.f, 4.f});
  ASSERT_NE(b, nullptr);
  EXPECT_TRUE(find_wall_junctions(*document.entity(b->id), document).empty());

  // 把 b 平移到 a 的终点上：平移本身不改局部几何，但交接变了，网格要跟着变。
  EntityTransform item;
  item.id = b->id;
  item.from = b->local_transform;
  item.to = translate({-6.f, 0.f, 0.f}) * b->local_transform;
  MoveEntitiesCommand command(document, {item});
  ASSERT_TRUE(command.execute());

  const Entity* moved = document.entity(b->id);
  ASSERT_NE(moved, nullptr);
  EXPECT_EQ(find_wall_junctions(*moved, document).size(), 1u);
  EXPECT_TRUE(mesh_covers(document, a->id, {4.03f, 1.5f, -0.06f}) ||
              mesh_covers(document, b->id, {4.03f, 1.5f, -0.06f}));
}

TEST(WallJunction, JoinedModelKeepsHostedOpeningCuts) {
  CornerWalls walls = make_corner();
  ASSERT_NE(walls.a, 0u);

  WindowEntity window({1.f, 1.2f, 0.f}, 1.2, 1.2, 0.08);
  auto geometry = window.createGeom();
  ASSERT_TRUE(geometry) << geometry.error();
  Entity* guest = walls.document.add_entity(std::make_unique<WindowEntity>(std::move(window)),
                                            std::move(*geometry));
  ASSERT_NE(guest, nullptr);
  ASSERT_TRUE(bind_opening_to_host(walls.document, guest->id, walls.a, {1.f, 1.2f, 0.f}));

  const Entity* wall = walls.document.entity(walls.a);
  ASSERT_NE(wall, nullptr);
  const FeatureModel model = wall_render_model(*wall, walls.document);
  bool has_miter_profile = false;
  bool has_cut = false;
  for (const Feature& feature : model.features()) {
    if (feature.kind == FeatureKind::PolygonProfile) {
      has_miter_profile = true;
    }
    if (feature.kind == FeatureKind::Boolean &&
        model.param(feature.id, "operation", 0.0) == 2.0) {
      has_cut = true;
    }
  }
  EXPECT_TRUE(has_miter_profile) << "倒角轮廓丢了";
  EXPECT_TRUE(has_cut) << "开口切减丢了";

  auto mesh = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(mesh) << mesh.error();
  EXPECT_FALSE(mesh->indices.empty());
}

TEST(WallJunction, RemovingNeighborRestoresFlatEnd) {
  CornerWalls walls = make_corner();
  ASSERT_NE(walls.a, 0u);
  ASSERT_NE(walls.b, 0u);
  EXPECT_GE(world_footprint(walls.document, walls.a).size(), 4u);

  walls.document.remove_entity(walls.b);
  const std::vector<Vec3> footprint = world_footprint(walls.document, walls.a);
  ASSERT_EQ(footprint.size(), 4u);
  for (const Vec3& p : footprint) {
    EXPECT_LE(p.x, 4.f + 1e-3f);
  }
  EXPECT_FALSE(mesh_covers(walls.document, walls.a, {4.03f, 1.5f, -0.06f}));
}

}  // namespace
}  // namespace tamias
