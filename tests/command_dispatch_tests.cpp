#include "command/core/command_system.h"
#include "command/import/import_texture_command.h"
#include "command/edit/update_material_command.h"
#include "bim/grid.h"
#include "engine/document/document.h"
#include "engine/modeling/feature/feature.h"
#include "engine/render/resource/texture_asset.h"
#include "entity/core/entity.h"
#include "entity/core/entity_storey.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>

namespace tamias {
namespace {

struct Cmd {
  CommandRegistry registry;
  CommandSystem system;
  Document doc;

  explicit Cmd(const char* name) : system(registry), doc(name) { register_commands(registry); }
};

void expect_undo_clears(Cmd& cmd) {
  ASSERT_TRUE(cmd.system.can_undo());
  cmd.system.undo();
  EXPECT_EQ(cmd.doc.entities().size(), 0u);
  EXPECT_TRUE(cmd.doc.meshes().empty());
  ASSERT_TRUE(cmd.system.can_redo());
  cmd.system.redo();
  EXPECT_EQ(cmd.doc.entities().size(), 1u);
}

TEST(CommandDispatch, CreateBeamFromPointsUndoRedo) {
  Cmd cmd("beam");
  CommandArgs args = {
      {"width", 0.3},
      {"depth", 0.5},
      {"points", std::vector<Vec3>{{0.f, 0.f, 0.f}, {4.f, 0.f, 0.f}}},
  };
  auto r = cmd.system.dispatch(cmd.doc, "create_beam", args);
  ASSERT_TRUE(r) << r.error();
  EXPECT_FALSE(cmd.system.has_pending());
  ASSERT_EQ(cmd.doc.entities().size(), 1u);
  EXPECT_EQ(cmd.doc.entities().begin()->second->kind(), EntityKind::Beam);
  EXPECT_FALSE(cmd.doc.meshes().empty());
  expect_undo_clears(cmd);
}

TEST(CommandDispatch, CreateColumnFromOrigin) {
  Cmd cmd("prims");
  auto r = cmd.system.dispatch(cmd.doc, "create_column", {{"origin", Vec3{1.f, 0.f, 2.f}}});
  ASSERT_TRUE(r) << r.error();
  EXPECT_FALSE(cmd.system.has_pending());
  ASSERT_EQ(cmd.doc.entities().size(), 1u);
  EXPECT_EQ(cmd.doc.entities().begin()->second->kind(), EntityKind::Column);
  cmd.system.undo();
  EXPECT_EQ(cmd.doc.entities().size(), 0u);
}

// 轴网布柱：3 根编号轴 × 2 根字母轴 = 6 个交点 → 6 根柱，整批一步撤销。
TEST(CommandDispatch, CreateColumnsOnGridPlacesOneColumnPerIntersection) {
  Cmd cmd("columns-on-grid");
  auto grid = cmd.system.dispatch(cmd.doc, "auto_grid",
                                  {{"x_spacings", std::vector<double>{6.0, 6.0}},
                                   {"z_spacings", std::vector<double>{5.0}},
                                   {"margin", 1.0}});
  ASSERT_TRUE(grid) << grid.error();

  auto r = cmd.system.dispatch(cmd.doc, "create_columns_on_grid",
                               {{"width", 0.5}, {"depth", 0.5}, {"height", 3.0}});
  ASSERT_TRUE(r) << r.error();
  EXPECT_FALSE(cmd.system.has_pending());
  ASSERT_EQ(cmd.doc.entities().size(), 6u);
  for (const auto& [id, entity] : cmd.doc.entities()) {
    (void)id;
    EXPECT_EQ(entity->kind(), EntityKind::Column);
  }
  // 一次布置 = 一步撤销：6 根柱一起退回去（轴网不是实体，还在）。
  cmd.system.undo();
  EXPECT_EQ(cmd.doc.entities().size(), 0u);
  EXPECT_EQ(cmd.doc.bim().grid().size(), 5u);
  cmd.system.redo();
  EXPECT_EQ(cmd.doc.entities().size(), 6u);
}

// 同一个交点上已经站着柱就不再重复布置（连点两次框选不会叠出两根柱）。
TEST(CommandDispatch, CreateColumnsOnGridSkipsOccupiedIntersections) {
  Cmd cmd("columns-on-grid-twice");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "auto_grid",
                                  {{"x_spacings", std::vector<double>{6.0}},
                                   {"z_spacings", std::vector<double>{5.0}}}));
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_columns_on_grid", {}));
  ASSERT_EQ(cmd.doc.entities().size(), 4u);  // 2 × 2

  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_columns_on_grid", {}));
  EXPECT_EQ(cmd.doc.entities().size(), 4u);  // 全部跳过，不再加
}

// 脚本式：axis_ids 只在框到的轴线之间求交，柱底标高取当前楼层。
TEST(CommandDispatch, CreateColumnsOnGridHonoursAxisIdsAndStorey) {
  Cmd cmd("columns-on-grid-ids");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "auto_grid",
                                  {{"x_spacings", std::vector<double>{6.0, 6.0}},
                                   {"z_spacings", std::vector<double>{5.0}},
                                   {"margin", 1.0}}));
  const std::vector<GridAxis>& axes = cmd.doc.bim().grid().axes();
  ASSERT_EQ(axes.size(), 5u);  // 3 编号轴（x = 0/6/12）+ 2 字母轴（z = 0/5）
  const std::uint64_t numbered_id = axes[1].id;  // x = 6
  const std::uint64_t lettered_id = axes[3].id;  // z = 0

  const std::uint64_t storey_id = cmd.doc.add_storey("2F", 3.0).id;
  cmd.doc.set_active_storey(storey_id);

  auto r = cmd.system.dispatch(
      cmd.doc, "create_columns_on_grid",
      {{"sub_type", std::string("circle")},
       {"diameter", 0.6},
       {"height", 3.0},
       {"axis_ids", std::vector<double>{static_cast<double>(numbered_id),
                                        static_cast<double>(lettered_id)}}});
  ASSERT_TRUE(r) << r.error();
  ASSERT_EQ(cmd.doc.entities().size(), 1u);
  const Entity& column = *cmd.doc.entities().begin()->second;
  EXPECT_EQ(column.kind(), EntityKind::Column);
  EXPECT_NEAR(column.local_transform(0, 3), 6.0, 1e-6);  // 交点 (6, 0)
  EXPECT_NEAR(column.local_transform(1, 3), 3.0, 1e-6);  // 本层标高
  EXPECT_NEAR(column.local_transform(2, 3), 0.0, 1e-6);
  EXPECT_EQ(entity_storey_id(column), storey_id);
}

TEST(CommandDispatch, CreateArcAndRectangleFromPoints) {
  Cmd cmd("sketches");
  {
    CommandArgs args = {
        {"points", std::vector<Vec3>{{0.f, 0.f, 0.f}, {1.f, 0.f, 1.f}, {2.f, 0.f, 0.f}}},
    };
    auto r = cmd.system.dispatch(cmd.doc, "create_arc", args);
    ASSERT_TRUE(r) << r.error();
    EXPECT_FALSE(cmd.system.has_pending());
    ASSERT_EQ(cmd.doc.entities().size(), 1u);
    EXPECT_EQ(cmd.doc.entities().begin()->second->kind(), EntityKind::Arc);
    cmd.system.undo();
  }
  {
    CommandArgs args = {
        {"points", std::vector<Vec3>{{0.f, 0.f, 0.f}, {2.f, 0.f, 1.f}}},
    };
    auto r = cmd.system.dispatch(cmd.doc, "create_rectangle", args);
    ASSERT_TRUE(r) << r.error();
    EXPECT_FALSE(cmd.system.has_pending());
    ASSERT_EQ(cmd.doc.entities().size(), 1u);
    EXPECT_EQ(cmd.doc.entities().begin()->second->kind(), EntityKind::Rectangle);
  }
}

TEST(CommandDispatch, CreateStoreyUndoRedo) {
  Cmd cmd("storey");
  EXPECT_TRUE(cmd.doc.bim().storeys().empty());
  auto r = cmd.system.dispatch(cmd.doc, "create_storey",
                               {{"name", std::string("2F")}, {"elevation", 3.0}});
  ASSERT_TRUE(r) << r.error();
  ASSERT_EQ(cmd.doc.bim().storeys().size(), 1u);
  EXPECT_EQ(cmd.doc.bim().storeys().front().name, "2F");
  EXPECT_NEAR(cmd.doc.bim().storeys().front().elevation, 3.0, 1e-9);
  EXPECT_EQ(cmd.doc.bim().active_storey_id(), cmd.doc.bim().storeys().front().id);

  ASSERT_TRUE(cmd.system.can_undo());
  cmd.system.undo();
  EXPECT_TRUE(cmd.doc.bim().storeys().empty());
  ASSERT_TRUE(cmd.system.can_redo());
  cmd.system.redo();
  ASSERT_EQ(cmd.doc.bim().storeys().size(), 1u);
  EXPECT_EQ(cmd.doc.bim().storeys().front().name, "2F");
}

TEST(CommandDispatch, SetLocationChangesElevationOffset) {
  Cmd cmd("set-loc");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_storey",
                                  {{"name", std::string("1F")}, {"elevation", 3.0}}));
  const std::uint64_t storey_id = cmd.doc.bim().storeys().front().id;
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_column", {{"origin", Vec3{0.f, 3.f, 0.f}}}));
  ASSERT_EQ(cmd.doc.entities().size(), 1u);
  const std::uint64_t eid = cmd.doc.entities().begin()->first;
  Entity* column = cmd.doc.entity(eid);
  ASSERT_NE(column, nullptr);
  ASSERT_NE(column->location, nullptr);
  EXPECT_EQ(column->location->storey_id(), storey_id);

  auto r = cmd.system.dispatch(
      cmd.doc, "set_location",
      {{"entity_id", static_cast<std::int64_t>(eid)},
       {"storey_id", static_cast<std::int64_t>(storey_id)},
       {"elevation_offset", 1.25}});
  ASSERT_TRUE(r) << r.error();
  EXPECT_NEAR(column->location->elevation_offset(), 1.25, 1e-6);
  EXPECT_NEAR(column->local_transform(1, 3), 4.25f, 1e-5f);

  cmd.system.undo();
  EXPECT_NEAR(column->location->elevation_offset(), 0.0, 1e-5);
}

TEST(CommandDispatch, SetMaterialUndoRestoresIdWithoutNewMesh) {
  Cmd cmd("mat");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_column", {{"origin", Vec3{0.f, 0.f, 0.f}}}));
  const std::uint64_t eid = cmd.doc.entities().begin()->first;
  Entity* box = cmd.doc.entity(eid);
  ASSERT_NE(box, nullptr);
  const std::uint64_t old_mat = box->material_id;
  const std::uint64_t mesh_id = box->mesh_asset_id;
  const std::size_t mesh_count = cmd.doc.meshes().size();

  auto r = cmd.system.dispatch(
      cmd.doc, "set_material",
      {{"entity_id", static_cast<std::int64_t>(eid)},
       {"name", std::string("Paint")},
       {"base_color", Vec3{0.2f, 0.4f, 0.8f}}});
  ASSERT_TRUE(r) << r.error();
  EXPECT_NE(box->material_id, old_mat);
  EXPECT_EQ(box->mesh_asset_id, mesh_id);
  EXPECT_EQ(cmd.doc.meshes().size(), mesh_count);

  cmd.system.undo();
  EXPECT_EQ(box->material_id, old_mat);
  cmd.system.redo();
  EXPECT_NE(box->material_id, old_mat);
}

TEST(CommandDispatch, ImportTextureUndoRemovesAdded) {
  Cmd cmd("import-tex");
  const std::size_t before = cmd.doc.textures().size();
  TextureAsset tex;
  tex.width = 2;
  tex.height = 2;
  tex.srgb = true;
  tex.usage = TextureUsage::Albedo;
  tex.name = "paint";
  tex.rgba.assign(16, 40);
  auto import = std::make_unique<ImportTextureCommand>(cmd.doc, std::move(tex));
  ASSERT_TRUE(import->execute());
  const std::uint64_t id = import->texture_id();
  EXPECT_EQ(cmd.doc.textures().size(), before + 1);
  cmd.system.push_executed(std::move(import));
  cmd.system.undo();
  EXPECT_EQ(cmd.doc.texture(id), nullptr);
  EXPECT_EQ(cmd.doc.textures().size(), before);
  cmd.system.redo();
  ASSERT_NE(cmd.doc.texture(id), nullptr);
  EXPECT_EQ(cmd.doc.texture(id)->name, "paint");
}

TEST(CommandDispatch, UpdateMaterialSharedUndo) {
  Cmd cmd("update-mat");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_column", {{"origin", Vec3{0.f, 0.f, 0.f}}}));
  Entity* box = cmd.doc.entities().begin()->second.get();
  ASSERT_NE(box, nullptr);
  Material shared{};
  shared.name = "SharedPaint";
  shared.base_color = {0.5f, 0.4f, 0.3f};
  const std::uint64_t mat_id = cmd.doc.add_material(std::move(shared)).id;
  box->material_id = mat_id;
  Material edited = *cmd.doc.material(mat_id);
  const Vec3 old_color = edited.base_color;
  edited.base_color = {0.1f, 0.2f, 0.3f};
  auto update = std::make_unique<UpdateMaterialCommand>(cmd.doc, edited);
  ASSERT_TRUE(update->execute());
  EXPECT_FLOAT_EQ(cmd.doc.material(box->material_id)->base_color.x, 0.1f);
  cmd.system.push_executed(std::move(update));
  cmd.system.undo();
  EXPECT_FLOAT_EQ(cmd.doc.material(box->material_id)->base_color.x, old_color.x);
}

TEST(CommandDispatch, ChamferUndoRedo) {
  Cmd cmd("chamfer");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_column", {{"origin", Vec3{0.f, 0.f, 0.f}}}));
  const std::uint64_t eid = cmd.doc.entities().begin()->first;
  EXPECT_EQ(cmd.doc.entity(eid)->model.features().size(), 2u);

  auto r = cmd.system.dispatch(
      cmd.doc, "chamfer",
      {{"entity_id", static_cast<std::int64_t>(eid)},
       {"distance", 0.05},
       {"edge", static_cast<std::int64_t>(0)}});
  ASSERT_TRUE(r) << r.error();
  EXPECT_EQ(cmd.doc.entity(eid)->model.features().size(), 3u);

  cmd.system.undo();
  EXPECT_EQ(cmd.doc.entity(eid)->model.features().size(), 2u);
  cmd.system.redo();
  EXPECT_EQ(cmd.doc.entity(eid)->model.features().size(), 3u);
}

TEST(CommandDispatch, BooleanCommonAndCutUndo) {
  Cmd cmd("bool-ops");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_column", {{"origin", Vec3{0.f, 0.f, 0.f}}}));
  const std::uint64_t box = cmd.doc.entities().begin()->first;
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_column",
                                  {{"origin", Vec3{0.f, 0.f, 0.f}},
                                   {"sub_type", std::string("circle")}}));
  std::uint64_t cyl = 0;
  for (const auto& [id, unused] : cmd.doc.entities()) {
    (void)unused;
    if (id != box) {
      cyl = id;
    }
  }
  ASSERT_NE(cyl, 0u);
  ASSERT_EQ(cmd.doc.entities().size(), 2u);

  auto cut = cmd.system.dispatch(
      cmd.doc, "boolean",
      {{"a", static_cast<std::int64_t>(box)},
       {"b", static_cast<std::int64_t>(cyl)},
       {"operation", static_cast<std::int64_t>(BooleanOp::Cut)}});
  ASSERT_TRUE(cut) << cut.error();
  EXPECT_EQ(cmd.doc.entities().size(), 1u);
  cmd.system.undo();
  EXPECT_EQ(cmd.doc.entities().size(), 2u);

  auto common = cmd.system.dispatch(
      cmd.doc, "boolean",
      {{"a", static_cast<std::int64_t>(box)},
       {"b", static_cast<std::int64_t>(cyl)},
       {"operation", static_cast<std::int64_t>(BooleanOp::Common)}});
  ASSERT_TRUE(common) << common.error();
  EXPECT_EQ(cmd.doc.entities().size(), 1u);
}

}  // namespace
}  // namespace tamias
