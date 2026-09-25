#include "bim/host_geometry.h"
#include "bim/host_update.h"
#include "bim/line_location.h"
#include "bim/wall_join.h"
#include "command/core/command_system.h"
#include "command/edit/copy_entities_command.h"
#include "command/edit/entity_transform.h"
#include "command/edit/mirror_entities_command.h"
#include "command/edit/transform_tool_command.h"
#include "engine/document/document.h"
#include "engine/graphics/mesh.h"
#include "engine/modeling/feature/curve_geom.h"
#include "entity/core/entity_grip.h"
#include "entity/family/attached/opening/door_entity.h"
#include "entity/family/host/architectural/wall_entity.h"
#include "entity/sketch/line_entity.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace tamias {
namespace {

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

Entity* add_door(Document& document, std::uint64_t wall_id, Vec3 at, double width = 1.0,
                 double height = 2.1) {
  DoorEntity door(at, width, height, 0.05, 0.0);
  auto geometry = door.createGeom();
  if (!geometry) {
    return nullptr;
  }
  Entity* added = document.add_entity(std::make_unique<DoorEntity>(std::move(door)),
                                      std::move(*geometry));
  if (added == nullptr || !bind_opening_to_host(document, added->id, wall_id, at)) {
    return nullptr;
  }
  return added;
}

// 造型里带几个开洞切减（Boolean 特征）。墙的渲染模型 = 墙-墙倒角 + 宿主开口切减，
// 所以「这面墙上有洞」等价于「渲染模型里有 Boolean 特征」。
int opening_cut_count(const Document& document, std::uint64_t wall_id) {
  const Entity* wall = document.entity(wall_id);
  if (wall == nullptr) {
    return 0;
  }
  const FeatureModel model = wall_render_model(*wall, document);
  int cuts = 0;
  for (const Feature& feature : model.features()) {
    if (feature.kind == FeatureKind::Boolean) {
      ++cuts;
    }
  }
  return cuts;
}

float determinant3(const Mat4& m) {
  return m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
         m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
         m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
}

std::uint64_t find_kind(const Document& document, const std::vector<std::uint64_t>& ids,
                        EntityKind kind) {
  for (const std::uint64_t id : ids) {
    const Entity* entity = document.entity(id);
    if (entity != nullptr && entity->kind() == kind) {
      return id;
    }
  }
  return 0;
}

}  // namespace

TEST(EditOperations, LinearArrayPlacements) {
  auto placements = linear_array_placements({1.f, 0.f, 0.f}, 2.0, 4);
  ASSERT_TRUE(placements) << placements.error();
  ASSERT_EQ(placements->size(), 3u);  // count 含原件 → 3 份副本
  EXPECT_FLOAT_EQ((*placements)[0](0, 3), 2.f);
  EXPECT_FLOAT_EQ((*placements)[2](0, 3), 6.f);
  EXPECT_FALSE(linear_array_placements({0.f, 0.f, 0.f}, 1.0, 3));  // 方向为 0
  EXPECT_FALSE(linear_array_placements({1.f, 0.f, 0.f}, 0.0, 3));  // 间距为 0
  EXPECT_FALSE(linear_array_placements({1.f, 0.f, 0.f}, 1.0, 1));  // 只有原件
}

TEST(EditOperations, PolarArrayPlacements) {
  auto placements = polar_array_placements({0.f, 0.f, 0.f}, 90.0, 3);
  ASSERT_TRUE(placements) << placements.error();
  ASSERT_EQ(placements->size(), 2u);
  // 第二份转 180°：+X 落到 -X。
  const Vec3 turned = (*placements)[1] * Vec3{1.f, 0.f, 0.f};
  EXPECT_NEAR(turned.x, -1.f, 1e-5f);
  EXPECT_NEAR(turned.z, 0.f, 1e-5f);
}

TEST(EditOperations, ArrayThroughRegistryKeepsOneMeshAndUndoRedo) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document document("array");
  Entity* wall = add_wall(document, {0.f, 0.f, 0.f}, {4.f, 0.f, 0.f});
  ASSERT_NE(wall, nullptr);
  const std::uint64_t wall_id = wall->id;
  const float before_origin = wall->local_transform(0, 3);
  ASSERT_EQ(document.entities().size(), 1u);
  const std::size_t meshes_before = document.meshes().size();

  ASSERT_TRUE(system.dispatch(document, "array_entities",
                              {{"ids", std::vector<double>{static_cast<double>(wall_id)}},
                               {"count", static_cast<std::int64_t>(3)},
                               {"spacing", 5.0},
                               {"direction", Vec3{1.f, 0.f, 0.f}}}));
  EXPECT_EQ(document.entities().size(), 3u);
  // 内容相同的副本走 intern：不额外占网格资产。
  EXPECT_EQ(document.meshes().size(), meshes_before);

  system.undo();
  EXPECT_EQ(document.entities().size(), 1u);
  system.redo();
  EXPECT_EQ(document.entities().size(), 3u);

  // 第三份落在「原件 + 2 × 间距」处（墙的局部原点是中点，不是端点）。
  bool found_far = false;
  for (const auto& [unused, entity] : document.entities()) {
    (void)unused;
    if (std::fabs(entity->local_transform(0, 3) - (before_origin + 10.f)) < 1e-4f) {
      found_far = true;
    }
  }
  EXPECT_TRUE(found_far);
}

// 环形阵列没给「每份夹角」时按总角度推：整圈按 count 均分，非整圈按 count-1 份。
TEST(EditOperations, PolarArrayDerivesStepFromTotalAngle) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document document("polar");
  Entity* wall = add_wall(document, {1.f, 0.f, 0.f}, {2.f, 0.f, 0.f});
  ASSERT_NE(wall, nullptr);
  const std::uint64_t wall_id = wall->id;

  ASSERT_TRUE(system.dispatch(document, "array_entities",
                              {{"ids", std::vector<double>{static_cast<double>(wall_id)}},
                               {"mode", std::string("polar")},
                               {"count", static_cast<std::int64_t>(2)},
                               {"total_angle", 360.0},
                               {"center", Vec3{0.f, 0.f, 0.f}}}));
  ASSERT_EQ(document.entities().size(), 2u);
  // 整圈 2 份 → 每份 180°：副本落到 -X 侧（原件在 +X）。
  bool found_back = false;
  for (const auto& [unused, entity] : document.entities()) {
    (void)unused;
    if (entity->local_transform(0, 3) < -0.5f) {
      found_back = true;
    }
  }
  EXPECT_TRUE(found_back);
}

TEST(EditOperations, CopyWallBringsItsOpenings) {
  Document document("copy");
  Entity* wall = add_wall(document, {0.f, 0.f, 0.f}, {6.f, 0.f, 0.f});
  ASSERT_NE(wall, nullptr);
  Entity* door = add_door(document, wall->id, {2.f, 0.f, 0.f});
  ASSERT_NE(door, nullptr);
  ASSERT_EQ(document.bim().relations().size(), 1u);

  CopyEntitiesCommand copy(document, {wall->id, door->id},
                           {translation_transform({0.f, 0.f, 10.f})});
  ASSERT_TRUE(copy.execute());
  EXPECT_EQ(document.entities().size(), 4u);
  EXPECT_EQ(document.bim().relations().size(), 2u);

  const std::uint64_t new_wall =
      find_kind(document, copy.created_ids(), EntityKind::Wall);
  const std::uint64_t new_door =
      find_kind(document, copy.created_ids(), EntityKind::Door);
  ASSERT_NE(new_wall, 0u);
  ASSERT_NE(new_door, 0u);

  // 副本里的门挂在**新的**墙上，而不是跟着原墙跑。
  const Relation* relation = document.bim().host_of(new_door);
  ASSERT_NE(relation, nullptr);
  EXPECT_EQ(relation->to, new_wall);
  EXPECT_TRUE(relation->valid);

  // 新墙上真的开了洞：渲染模型里有开洞切减，和源墙一致。
  EXPECT_EQ(opening_cut_count(document, new_wall), 1);
  // 而且几何与源墙逐位一致（内容哈希 intern 到同一个网格资产）。比打射线数交点稳：
  // 墙-洞的三角化里有边正好穿过墙中点，射线法会在边上抖。
  const Entity* copied_wall = document.entity(new_wall);
  const Entity* source_wall = document.entity(wall->id);
  ASSERT_NE(copied_wall, nullptr);
  ASSERT_NE(source_wall, nullptr);
  EXPECT_EQ(copied_wall->mesh_asset_id, source_wall->mesh_asset_id);

  // 副本里的门也落在「源门位置 + 位移」上。
  const Entity* copied_door = document.entity(new_door);
  const Entity* source_door = document.entity(door->id);
  ASSERT_NE(copied_door, nullptr);
  ASSERT_NE(source_door, nullptr);
  EXPECT_NEAR(copied_door->local_transform(2, 3), source_door->local_transform(2, 3) + 10.f,
              1e-3f);

  copy.undo();
  EXPECT_EQ(document.entities().size(), 2u);
  EXPECT_EQ(document.bim().relations().size(), 1u);
  // 原墙的开洞和宿主关系都还在。
  EXPECT_EQ(opening_cut_count(document, wall->id), 1);
  ASSERT_NE(document.bim().host_of(door->id), nullptr);
  EXPECT_EQ(document.bim().host_of(door->id)->to, wall->id);

  copy.redo();
  EXPECT_EQ(document.entities().size(), 4u);
  EXPECT_EQ(document.bim().relations().size(), 2u);
  EXPECT_EQ(opening_cut_count(document, wall->id), 1);
}

TEST(EditOperations, CopySketchCurveKeepsPlacement) {
  Document document("copy-sketch");
  LineEntity line({0.f, 0.f, 0.f}, {2.f, 0.f, 0.f});
  auto geometry = line.createGeom();
  ASSERT_TRUE(geometry) << geometry.error();
  Entity* source = document.add_entity(std::make_unique<LineEntity>(std::move(line)),
                                       std::move(*geometry));
  ASSERT_NE(source, nullptr);

  CopyEntitiesCommand copy(document, {source->id},
                           {translation_transform({0.f, 0.f, 3.f})});
  ASSERT_TRUE(copy.execute());
  ASSERT_EQ(copy.created_ids().size(), 1u);
  const Entity* made = document.entity(copy.created_ids().front());
  ASSERT_NE(made, nullptr);
  const std::vector<EntityGrip> grips = collect_entity_grips(*made);
  ASSERT_EQ(grips.size(), 2u);
  EXPECT_NEAR(grips[0].world.z, 3.f, 1e-4f);
  EXPECT_NEAR(grips[1].world.x, 2.f, 1e-4f);
}

TEST(EditOperations, MirrorBakesIntoGeometryAndKeepsTransformRigid) {
  Document document("mirror");
  Entity* wall = add_wall(document, {1.f, 0.f, 0.f}, {5.f, 0.f, 0.f});
  ASSERT_NE(wall, nullptr);

  // 镜像轴 = 过原点沿 +Z → 平面是 X = 0。
  MirrorEntitiesCommand mirror(document, {wall->id}, {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f});
  ASSERT_TRUE(mirror.execute());

  const Entity* moved = document.entity(wall->id);
  ASSERT_NE(moved, nullptr);
  ASSERT_NE(moved->location, nullptr);
  const auto* line = static_cast<const LineLocation*>(moved->location.get());
  EXPECT_NEAR(std::min(line->start().x, line->end().x), -5.f, 1e-3f);
  EXPECT_NEAR(std::max(line->start().x, line->end().x), -1.f, 1e-3f);
  // 关键：摆放仍是刚体（右手系），镜像被烘焙进几何而不是写成反射矩阵。
  EXPECT_NEAR(determinant3(moved->local_transform), 1.f, 1e-4f);

  mirror.undo();
  const Entity* back = document.entity(wall->id);
  ASSERT_NE(back, nullptr);
  const auto* back_line = static_cast<const LineLocation*>(back->location.get());
  EXPECT_NEAR(std::min(back_line->start().x, back_line->end().x), 1.f, 1e-3f);
  EXPECT_NEAR(std::max(back_line->start().x, back_line->end().x), 5.f, 1e-3f);
}

TEST(EditOperations, RotateToolThreePoints) {
  Document document("rotate");
  Entity* wall = add_wall(document, {1.f, 0.f, 0.f}, {5.f, 0.f, 0.f});
  ASSERT_NE(wall, nullptr);

  TransformToolCommand tool(document, TransformToolCommand::Mode::Rotate, {wall->id});
  ASSERT_TRUE(tool.interactive());
  auto first = tool.on_point({0.f, 0.f, 0.f});   // 基点
  ASSERT_TRUE(first) << first.error();
  EXPECT_FALSE(*first);
  auto second = tool.on_point({1.f, 0.f, 0.f});  // 参照方向 = +X
  ASSERT_TRUE(second) << second.error();
  EXPECT_FALSE(*second);
  auto third = tool.on_point({0.f, 0.f, 1.f});   // 目标方向 = +Z
  ASSERT_TRUE(third) << third.error();
  EXPECT_TRUE(*third);

  ASSERT_TRUE(tool.execute());
  const Entity* turned = document.entity(wall->id);
  ASSERT_NE(turned, nullptr);
  const auto* line = static_cast<const LineLocation*>(turned->location.get());
  // +X 方向转到 +Z：墙从 (1,0,0)-(5,0,0) 变成 (0,0,1)-(0,0,5)。
  EXPECT_NEAR(line->start().z, 1.f, 1e-3f);
  EXPECT_NEAR(line->end().z, 5.f, 1e-3f);
  EXPECT_NEAR(line->end().x, 0.f, 1e-3f);

  tool.undo();
  const Entity* restored = document.entity(wall->id);
  ASSERT_NE(restored, nullptr);
  const auto* restored_line = static_cast<const LineLocation*>(restored->location.get());
  EXPECT_NEAR(restored_line->end().x, 5.f, 1e-3f);
}

TEST(EditOperations, MoveThroughRegistryByDelta) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document document("move");
  Entity* wall = add_wall(document, {0.f, 0.f, 0.f}, {4.f, 0.f, 0.f});
  ASSERT_NE(wall, nullptr);
  const std::uint64_t wall_id = wall->id;
  const Mat4 before = wall->local_transform;

  ASSERT_TRUE(system.dispatch(document, "move_entities",
                              {{"ids", std::vector<double>{static_cast<double>(wall_id)}},
                               {"delta", Vec3{2.f, 0.f, 1.f}}}));
  const Entity* moved = document.entity(wall_id);
  ASSERT_NE(moved, nullptr);
  EXPECT_FLOAT_EQ(moved->local_transform(0, 3), before(0, 3) + 2.f);
  EXPECT_FLOAT_EQ(moved->local_transform(2, 3), before(2, 3) + 1.f);

  system.undo();
  const Entity* back = document.entity(wall_id);
  ASSERT_NE(back, nullptr);
  EXPECT_FLOAT_EQ(back->local_transform(0, 3), before(0, 3));
}

}  // namespace tamias
