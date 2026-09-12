#include "bim/drawing_import.h"
#include "command/command_system.h"
#include "command/import_drawing_command.h"
#include "engine/document/document.h"
#include "engine/drawing/dxf_reader.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tamias {
namespace {

// 样例图是仓库里那张 12m×8m 的平面图：墙（WALL 层）、圆柱（COLUMN）、
// 门块（DOOR）、房间名（TEXT）。它正好是翻模的金样输入。
Drawing sample_plan() {
  const auto path = std::filesystem::path(TAMIAS_SOURCE_DIR) / "assets" / "samples" /
                    "drawings" / "floor-plan-sample.dxf";
  auto result = load_dxf(path);
  EXPECT_TRUE(result) << (result ? "" : result.error());
  return result ? std::move(*result) : Drawing{};
}

DrawingImportOptions sample_options() {
  DrawingImportOptions options;
  // 样例图没写 $INSUNITS，正文里全是毫米；显式覆盖单位（也说明"图纸没单位"这条路）。
  options.unit_scale = 0.001;
  return options;
}

std::string group(int code, std::string_view value) {
  return std::to_string(code) + "\n" + std::string(value) + "\n";
}

Drawing dxf_entities(const std::string& entities) {
  auto result = parse_dxf("0\nSECTION\n2\nENTITIES\n" + entities + "0\nENDSEC\n0\nEOF\n");
  EXPECT_TRUE(result) << (result ? "" : result.error());
  return result ? std::move(*result) : Drawing{};
}

}  // namespace

TEST(DrawingImport, SamplePlanYieldsWallsColumnsAndDoor) {
  const Drawing drawing = sample_plan();
  DrawingImportInfo info;
  const DrawingImportPlan plan = build_drawing_import_plan(drawing, sample_options(), nullptr, &info);

  // 外轮廓 4 段 + 两道内墙；WALL 层那条带凸度的圆弧太短，按图例跳过。
  EXPECT_EQ(plan.walls.size(), 6u);
  // 三根 R300 圆柱 → 直径 0.6 m。
  ASSERT_EQ(plan.columns.size(), 3u);
  for (const ColumnCandidate& column : plan.columns) {
    EXPECT_TRUE(column.circular);
    EXPECT_NEAR(column.width, 0.6, 1e-3);
  }
  // 一樘门，落在 y = 4000 那道内墙上。
  ASSERT_EQ(plan.openings.size(), 1u);
  const OpeningCandidate& door = plan.openings.front();
  EXPECT_TRUE(door.door);
  EXPECT_EQ(door.block, "DOOR_SWING");
  EXPECT_NEAR(door.position.z, 4.0, 1e-3f);
  EXPECT_NEAR(door.width, 0.81, 0.02);  // 门块外接框 900×0.9
  EXPECT_EQ(plan.walls[door.host_wall].start.z, 4.f);

  EXPECT_NEAR(info.unit_scale, 0.001, 1e-9);
  EXPECT_FALSE(info.layers.empty());
  EXPECT_FALSE(plan.warnings.empty());  // 单位没写 + 圆弧跳过 + 梁板未识别
}

TEST(DrawingImport, GridSnapPullsWallEndsOntoAbsoluteAxes) {
  // 图纸坐标 (200,0)-(6000,0)，即 0.2 m → 6 m。
  std::string entities;
  entities += group(0, "LINE") + group(8, "WALL") + group(10, "200") + group(20, "0") +
              group(11, "6000") + group(21, "0");
  const Drawing drawing = dxf_entities(entities);

  Grid grid;
  GridAxis origin;
  origin.name = "1";
  origin.direction = GridAxisDirection::AlongZ;
  origin.position = 0.0;
  origin.start = -1.0;
  origin.end = 10.0;
  grid.add(origin);
  GridAxis far;
  far.name = "2";
  far.direction = GridAxisDirection::AlongZ;
  far.position = 6.0;
  far.start = -1.0;
  far.end = 10.0;
  grid.add(far);

  DrawingImportOptions options = sample_options();
  options.grid_snap_tolerance = 0.25;
  const DrawingImportPlan plan = build_drawing_import_plan(drawing, options, &grid);
  ASSERT_EQ(plan.walls.size(), 1u);
  // 0.2 m 与 0 相差在容差内 → 吸到 0；6 m 正好落在 2 号轴上。
  EXPECT_NEAR(plan.walls.front().start.x, 0.0, 1e-4f);
  EXPECT_NEAR(plan.walls.front().end.x, 6.0, 1e-4f);

  // 关掉吸附就该保持原样：0.2 → 6.0。
  options.align_to_grid = false;
  const DrawingImportPlan raw = build_drawing_import_plan(drawing, options, &grid);
  ASSERT_EQ(raw.walls.size(), 1u);
  EXPECT_NEAR(raw.walls.front().start.x, 0.2, 1e-4f);
}

TEST(DrawingImport, FilterDropsOpeningsWhoseWallIsUnchecked) {
  DrawingImportPlan plan;
  plan.walls.resize(2);
  plan.columns.resize(1);
  OpeningCandidate opening;
  opening.host_wall = 1;
  plan.openings.push_back(opening);

  // 去掉 1 号墙：门窗没有宿主了，必须一起丢，不能挂到 0 号墙上。
  std::size_t dropped = 0;
  const DrawingImportPlan filtered =
      filter_drawing_import_plan(plan, {true, false}, {true}, {true}, &dropped);
  EXPECT_EQ(filtered.walls.size(), 1u);
  EXPECT_EQ(filtered.openings.size(), 0u);
  EXPECT_EQ(dropped, 1u);

  // 保留 1 号墙但去掉 0 号：宿主下标要重映射到 0。
  const DrawingImportPlan remapped =
      filter_drawing_import_plan(plan, {false, true}, {false}, {true});
  ASSERT_EQ(remapped.walls.size(), 1u);
  ASSERT_EQ(remapped.openings.size(), 1u);
  EXPECT_EQ(remapped.openings.front().host_wall, 0u);
}

TEST(DrawingImport, CommandAppliesAndUndoesAsOneStep) {
  const Drawing drawing = sample_plan();
  DrawingImportPlan plan = build_drawing_import_plan(drawing, sample_options(), nullptr);
  ASSERT_FALSE(plan.empty());

  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);
  Document document("import");

  auto command = std::make_unique<ImportDrawingCommand>(document, plan);
  ASSERT_TRUE(command->execute());
  const std::size_t walls = plan.walls.size();
  const std::size_t columns = plan.columns.size();
  const std::size_t openings = command->created_openings();
  ASSERT_EQ(document.entities().size(), walls + columns + openings);
  EXPECT_EQ(openings, 1u);
  EXPECT_FALSE(document.bim().relations().empty());
  EXPECT_FALSE(document.meshes().empty());

  const std::size_t entity_count = document.entities().size();
  system.push_executed(std::move(command));

  // 一次撤销：整批构件一起消失。
  system.undo();
  EXPECT_TRUE(document.entities().empty());
  EXPECT_TRUE(document.bim().relations().empty());

  // 一次重做：连句柄一起回来（数量、id 都不变）。
  system.redo();
  EXPECT_EQ(document.entities().size(), entity_count);
}

}  // namespace tamias
