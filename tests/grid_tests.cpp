#include "bim/grid.h"
#include "command/core/command_system.h"
#include "command/edit/update_grid_command.h"
#include "engine/document/document.h"
#include "engine/document/document_io.h"
#include "engine/document/picking.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tamias {
namespace {

struct Cmd {
  CommandRegistry registry;
  CommandSystem system;
  Document doc;

  explicit Cmd(const char* name) : system(registry), doc(name) { register_commands(registry); }
};

std::filesystem::path temp_dir() {
  const auto dir = std::filesystem::temp_directory_path() / "tamias_grid_tests";
  std::filesystem::create_directories(dir);
  return dir;
}

// 屏幕投影用的最小假视口：world.x → 屏幕 +x、world.z → 屏幕 −y，即 800×600 的平面
// 视图，世界 10 m 铺满一屏（水平 40 px/m、竖直 30 px/m）。depth 不参与，只判 w > 0。
Mat4 plan_view_proj() {
  Mat4 m{};
  m(0, 0) = 0.1f;  // x_clip = 0.1 · world.x
  m(1, 2) = 0.1f;  // y_clip = 0.1 · world.z
  m(3, 3) = 1.f;   // w = 1
  return m;
}

// 轴线「1」竖直（沿 Z，x = 0，z 从 -10 到 +10）；轴线「A」水平（沿 X，z = 0，x 从 -10 到 +10）。
Grid two_crossing_axes() {
  Grid grid;
  GridAxis numbered;
  numbered.name = "1";
  numbered.direction = GridAxisDirection::AlongZ;
  numbered.position = 0.0;
  numbered.start = -10.0;
  numbered.end = 10.0;
  grid.add(numbered);
  GridAxis lettered;
  lettered.name = "A";
  lettered.direction = GridAxisDirection::AlongX;
  lettered.position = 0.0;
  lettered.start = -10.0;
  lettered.end = 10.0;
  grid.add(lettered);
  return grid;
}

}  // namespace

TEST(Grid, BuildsOrthogonalGridFromSpacings) {
  const std::vector<GridAxis> axes = make_orthogonal_grid(0.0, 0.0, {6.0, 6.0, 6.0}, {5.0, 5.0}, 1.0);
  ASSERT_EQ(axes.size(), 7u);

  // 编号轴（沿 Z）落在 x = 0 / 6 / 12 / 18，名称 1..4。
  for (int i = 0; i < 4; ++i) {
    const GridAxis& axis = axes[static_cast<std::size_t>(i)];
    EXPECT_EQ(axis.direction, GridAxisDirection::AlongZ);
    EXPECT_EQ(axis.name, std::to_string(i + 1));
    EXPECT_DOUBLE_EQ(axis.position, 6.0 * i);
    EXPECT_DOUBLE_EQ(axis.start, -1.0);  // z 跨度 0..10 再外扩 1
    EXPECT_DOUBLE_EQ(axis.end, 11.0);
  }

  // 字母轴（沿 X）落在 z = 0 / 5 / 10，名称 A/B/C。
  const char* names[] = {"A", "B", "C"};
  for (int i = 0; i < 3; ++i) {
    const GridAxis& axis = axes[static_cast<std::size_t>(i) + 4];
    EXPECT_EQ(axis.direction, GridAxisDirection::AlongX);
    EXPECT_EQ(axis.name, names[i]);
    EXPECT_DOUBLE_EQ(axis.position, 5.0 * i);
    EXPECT_DOUBLE_EQ(axis.start, -1.0);  // x 跨度 0..18 再外扩 1
    EXPECT_DOUBLE_EQ(axis.end, 19.0);
  }
}

TEST(Grid, SingleDirectionSpacingsDoNotInventTheOtherSide) {
  const std::vector<GridAxis> axes = make_orthogonal_grid(0.0, 0.0, {6.0}, {}, 1.0);
  ASSERT_EQ(axes.size(), 2u);
  EXPECT_EQ(axes[0].direction, GridAxisDirection::AlongZ);
  EXPECT_EQ(axes[1].direction, GridAxisDirection::AlongZ);
}

TEST(Grid, SnapPlanPointPullsOntoNearestAxis) {
  Grid grid;
  GridAxis a;
  a.name = "1";
  a.direction = GridAxisDirection::AlongZ;
  a.position = 6.0;
  a.start = -1.0;
  a.end = 11.0;
  grid.add(a);
  GridAxis b;
  b.name = "A";
  b.direction = GridAxisDirection::AlongX;
  b.position = 5.0;
  b.start = -1.0;
  b.end = 19.0;
  grid.add(b);

  bool snapped = false;
  const Vec2 hit = grid.snap_plan({6.05f, 5.10f}, 0.2, &snapped);
  EXPECT_TRUE(snapped);
  EXPECT_FLOAT_EQ(hit.x, 6.f);
  EXPECT_FLOAT_EQ(hit.y, 5.f);

  snapped = true;
  const Vec2 miss = grid.snap_plan({8.f, 9.f}, 0.2, &snapped);
  EXPECT_FALSE(snapped);
  EXPECT_FLOAT_EQ(miss.x, 8.f);
  EXPECT_FLOAT_EQ(miss.y, 9.f);
}

TEST(Grid, SegmentsArePairsAndBoundsCoverAxes) {
  Grid grid;
  GridAxis a;
  a.direction = GridAxisDirection::AlongZ;
  a.position = 2.0;
  a.start = 0.0;
  a.end = 10.0;
  grid.add(a);

  std::vector<Vec3> segments;
  grid.append_segments(segments);
  ASSERT_EQ(segments.size(), 2u);
  EXPECT_FLOAT_EQ(segments[0].x, 2.f);
  EXPECT_FLOAT_EQ(segments[0].z, 0.f);
  EXPECT_FLOAT_EQ(segments[1].z, 10.f);

  const Aabb box = grid.bounds();
  ASSERT_TRUE(box.valid());
  EXPECT_FLOAT_EQ(box.min.x, 2.f);
  EXPECT_FLOAT_EQ(box.max.z, 10.f);
}

TEST(Grid, AppendAxisSegmentsSkipsZeroLengthAxes) {
  std::vector<GridAxis> axes;
  GridAxis good;
  good.direction = GridAxisDirection::AlongZ;
  good.position = 2.0;
  good.start = 0.0;
  good.end = 10.0;
  axes.push_back(good);
  GridAxis degenerate = good;
  degenerate.start = 5.0;
  degenerate.end = 5.0;
  axes.push_back(degenerate);  // 长度 0：不成线段，跳过

  std::vector<Vec3> segments;
  append_axis_segments(axes, segments);
  ASSERT_EQ(segments.size(), 2u);
  EXPECT_FLOAT_EQ(segments[0].x, 2.f);
  EXPECT_FLOAT_EQ(segments[0].z, 0.f);
  EXPECT_FLOAT_EQ(segments[1].z, 10.f);
}

TEST(Grid, TranslateMovesFixedCoordinateAndRangePerDirection) {
  std::vector<GridAxis> axes;
  GridAxis numbered;  // 编号轴：固定 x，范围沿 z
  numbered.name = "1";
  numbered.direction = GridAxisDirection::AlongZ;
  numbered.position = 6.0;
  numbered.start = -1.0;
  numbered.end = 11.0;
  axes.push_back(numbered);
  GridAxis lettered;  // 字母轴：固定 z，范围沿 x
  lettered.name = "A";
  lettered.direction = GridAxisDirection::AlongX;
  lettered.position = 5.0;
  lettered.start = -1.0;
  lettered.end = 19.0;
  axes.push_back(lettered);

  translate_grid(axes, 10.0, -2.0);

  EXPECT_DOUBLE_EQ(axes[0].position, 16.0);  // x + dx
  EXPECT_DOUBLE_EQ(axes[0].start, -3.0);     // z + dz
  EXPECT_DOUBLE_EQ(axes[0].end, 9.0);
  EXPECT_DOUBLE_EQ(axes[1].position, 3.0);   // z + dz
  EXPECT_DOUBLE_EQ(axes[1].start, 9.0);      // x + dx
  EXPECT_DOUBLE_EQ(axes[1].end, 29.0);
}

TEST(Grid, SelectionToggleClearAndList) {
  Grid grid = two_crossing_axes();
  const std::uint64_t numbered = grid.axes()[0].id;
  const std::uint64_t lettered = grid.axes()[1].id;
  ASSERT_NE(numbered, 0u);
  EXPECT_FALSE(grid.has_selection());

  grid.select(lettered);
  grid.select(numbered);
  EXPECT_TRUE(grid.axis_selected(numbered));
  EXPECT_TRUE(grid.has_selection());
  EXPECT_EQ(grid.selected_ids(), (std::vector<std::uint64_t>{numbered, lettered}));

  grid.deselect(numbered);
  EXPECT_FALSE(grid.axis_selected(numbered));
  EXPECT_EQ(grid.selected_ids(), (std::vector<std::uint64_t>{lettered}));

  grid.clear_selection();
  EXPECT_FALSE(grid.has_selection());
  EXPECT_TRUE(grid.selected_ids().empty());
}

TEST(Grid, PickAxisUsesScreenDistanceNotWorldDistance) {
  const Grid grid = two_crossing_axes();
  const std::uint64_t numbered = grid.axes()[0].id;  // 屏幕上是 x = 400 那条竖线
  const GridAxis degenerate = [] {
    GridAxis axis;
    axis.name = "9";
    axis.direction = GridAxisDirection::AlongZ;
    axis.position = 0.0;
    axis.start = 3.0;
    axis.end = 3.0;
    return axis;  // 长度 0：不参与拾取
  }();

  // 竖线旁边 5 px：容差内命中「1」；此时离横轴 A 有 200 px，选的是近的那根。
  EXPECT_EQ(pick_grid_axis_on_screen(grid.axes(), plan_view_proj(), 800.f, 600.f, 0.f, 405.f,
                                     100.f, 8.f),
            numbered);
  // 交点处两条轴都在容差里：取先遍历到的（轴 1）。
  EXPECT_EQ(pick_grid_axis_on_screen(grid.axes(), plan_view_proj(), 800.f, 600.f, 0.f, 400.f,
                                     300.f, 8.f),
            numbered);
  // 离轴很远：不命中。
  EXPECT_EQ(pick_grid_axis_on_screen(grid.axes(), plan_view_proj(), 800.f, 600.f, 0.f, 100.f,
                                     500.f, 8.f),
            0u);
  // 零长轴永远点不中，哪怕光标正压在上面。
  EXPECT_EQ(pick_grid_axis_on_screen({degenerate}, plan_view_proj(), 800.f, 600.f, 0.f, 400.f,
                                     30.f, 8.f),
            0u);
}

TEST(Grid, BoxSelectDistinguishesWindowAndCrossing) {
  const Grid grid = two_crossing_axes();
  const std::uint64_t numbered = grid.axes()[0].id;
  const std::uint64_t lettered = grid.axes()[1].id;
  const Mat4 vp = plan_view_proj();

  // 全屏 window：两条轴整体都在框里。
  EXPECT_EQ(grid_axes_in_screen_rect(grid.axes(), vp, 800.f, 600.f, 0.f, 0.f, 0.f, 800.f, 600.f,
                                     /*crossing=*/false),
            (std::vector<std::uint64_t>{numbered, lettered}));

  // 右侧竖条：只有横轴 A 与框相交（它横穿整个屏幕），两条轴都不在框内。
  EXPECT_TRUE(grid_axes_in_screen_rect(grid.axes(), vp, 800.f, 600.f, 0.f, 420.f, 0.f, 780.f,
                                       600.f, /*crossing=*/false)
                  .empty());
  EXPECT_EQ(grid_axes_in_screen_rect(grid.axes(), vp, 800.f, 600.f, 0.f, 420.f, 0.f, 780.f, 600.f,
                                     /*crossing=*/true),
            (std::vector<std::uint64_t>{lettered}));

  // 交点附近的小框：crossing 两条都要，window 一条都不要。
  EXPECT_EQ(grid_axes_in_screen_rect(grid.axes(), vp, 800.f, 600.f, 0.f, 395.f, 295.f, 405.f,
                                     305.f, /*crossing=*/true)
                .size(),
            2u);
  EXPECT_TRUE(grid_axes_in_screen_rect(grid.axes(), vp, 800.f, 600.f, 0.f, 395.f, 295.f, 405.f,
                                       305.f, /*crossing=*/false)
                  .empty());
}

// 放置：整张表平移后落盘，锚点（生成原点）正好停在点击的那个平面上。
TEST(Grid, PlacedGridPutsOriginOnTheClickPoint) {
  Document doc("grid-place");
  std::vector<GridAxis> table = make_orthogonal_grid(0.0, 0.0, {6.0, 6.0}, {5.0}, 1.0);
  const Vec3 click{12.5f, 0.f, -3.25f};
  translate_grid(table, click.x, click.z);

  UpdateGridCommand command(doc, table);
  ASSERT_TRUE(command.execute());

  const std::vector<GridAxis>& axes = doc.bim().grid().axes();
  ASSERT_EQ(axes.size(), 5u);
  EXPECT_DOUBLE_EQ(axes.front().position, 12.5);  // 编号轴 1 落在 x = 12.5
  EXPECT_DOUBLE_EQ(axes.front().start, -4.25);    // 范围随之平移
  EXPECT_EQ(axes[3].name, "A");
  EXPECT_DOUBLE_EQ(axes[3].position, -3.25);  // 字母轴 A 落在 z = -3.25
  EXPECT_DOUBLE_EQ(axes[3].start, 11.5);      // 范围沿 x 一起挪

  command.undo();
  EXPECT_TRUE(doc.bim().grid().empty());
}

TEST(Grid, AutoGridDispatchUndoRedo) {
  Cmd cmd("grid");
  CommandArgs args = {
      {"x_spacings", std::vector<double>{6.0, 6.0}},
      {"z_spacings", std::vector<double>{5.0}},
      {"margin", 1.0},
  };
  auto r = cmd.system.dispatch(cmd.doc, "auto_grid", args);
  ASSERT_TRUE(r) << r.error();
  // x: 0/6/12 三根编号轴 + z: 0/5 两根字母轴 = 5
  ASSERT_EQ(cmd.doc.bim().grid().size(), 5u);

  cmd.system.undo();
  EXPECT_TRUE(cmd.doc.bim().grid().empty());
  cmd.system.redo();
  EXPECT_EQ(cmd.doc.bim().grid().size(), 5u);
}

TEST(Grid, CreateAndDeleteAxisCommandsAreReversible) {
  Cmd cmd("axis");
  CommandArgs args = {
      {"name", std::string("3")},
      {"direction", std::string("z")},
      {"position", 12.0},
      {"start", -1.0},
      {"end", 11.0},
  };
  auto r = cmd.system.dispatch(cmd.doc, "create_grid_axis", args);
  ASSERT_TRUE(r) << r.error();
  ASSERT_EQ(cmd.doc.bim().grid().size(), 1u);
  const std::uint64_t id = cmd.doc.bim().grid().axes().front().id;
  EXPECT_NE(id, 0u);

  auto del = cmd.system.dispatch(cmd.doc, "delete_grid_axis", {{"axis_id", static_cast<std::int64_t>(id)}});
  ASSERT_TRUE(del) << del.error();
  EXPECT_TRUE(cmd.doc.bim().grid().empty());

  cmd.system.undo();  // 撤销删除
  ASSERT_EQ(cmd.doc.bim().grid().size(), 1u);
  EXPECT_EQ(cmd.doc.bim().grid().axes().front().id, id);
  EXPECT_EQ(cmd.doc.bim().grid().axes().front().name, "3");

  cmd.system.undo();  // 撤销创建
  EXPECT_TRUE(cmd.doc.bim().grid().empty());
  cmd.system.redo();
  ASSERT_EQ(cmd.doc.bim().grid().size(), 1u);
}

TEST(Grid, UpdateCommandKeepsHandlesForRedo) {
  Document doc("grid");
  GridAxis fresh;
  fresh.name = "1";
  fresh.direction = GridAxisDirection::AlongZ;
  fresh.position = 0.0;
  fresh.start = -1.0;
  fresh.end = 5.0;
  std::vector<GridAxis> plan{fresh};

  UpdateGridCommand command(doc, plan);
  ASSERT_TRUE(command.execute());
  ASSERT_EQ(doc.bim().grid().size(), 1u);
  const std::uint64_t first_id = doc.bim().grid().axes().front().id;
  ASSERT_NE(first_id, 0u);

  command.undo();
  EXPECT_TRUE(doc.bim().grid().empty());
  command.redo();
  ASSERT_EQ(doc.bim().grid().size(), 1u);
  EXPECT_EQ(doc.bim().grid().axes().front().id, first_id);  // redo 不换句柄
}

TEST(Grid, SurvivesDocumentRoundTrip) {
  Cmd cmd("grid-io");
  auto r = cmd.system.dispatch(cmd.doc, "auto_grid",
                               {{"x_spacings", std::vector<double>{6.0, 6.0}},
                                {"z_spacings", std::vector<double>{5.0, 5.0}},
                                {"margin", 1.0}});
  ASSERT_TRUE(r) << r.error();
  // x: 0/6/12 三根编号轴 + z: 0/5/10 三根字母轴 = 6
  ASSERT_EQ(cmd.doc.bim().grid().size(), 6u);

  // 内存快照（undo 用的整文档体）。
  auto bytes = serialize_document(cmd.doc);
  ASSERT_TRUE(bytes) << bytes.error();
  auto restored = deserialize_document(*bytes);
  ASSERT_TRUE(restored) << restored.error();
  ASSERT_EQ(restored->bim().grid().size(), 6u);
  EXPECT_EQ(restored->bim().grid().axes().front().name,
            cmd.doc.bim().grid().axes().front().name);
  EXPECT_DOUBLE_EQ(restored->bim().grid().axes().front().position,
                   cmd.doc.bim().grid().axes().front().position);

  // 文件格式（GRID chunk）。
  const auto path = temp_dir() / "grid_roundtrip.tdoc";
  ViewportState viewport{};
  ASSERT_TRUE(save_document(path, cmd.doc, viewport));
  auto loaded = load_document(path);
  ASSERT_TRUE(loaded) << loaded.error();
  ASSERT_EQ(loaded->document.bim().grid().size(), 6u);
  EXPECT_EQ(loaded->document.bim().grid().axes().back().name,
            cmd.doc.bim().grid().axes().back().name);
  EXPECT_DOUBLE_EQ(loaded->document.bim().grid().axes().back().position,
                   cmd.doc.bim().grid().axes().back().position);
  EXPECT_GE(loaded->document.bim().grid().next_id(), 7u);
}

// 轴交点（「轴网布置」的几何底子）：编号轴 × 字母轴的笛卡尔积。
TEST(GridIntersections, CartesianProductOfBothDirections) {
  // 3 根编号轴（x = 0 / 6 / 12）× 2 根字母轴（z = 0 / 5）= 6 个交点。
  const std::vector<GridAxis> axes =
      make_orthogonal_grid(0.0, 0.0, {6.0, 6.0}, {5.0}, 1.0);
  const std::vector<Vec3> points = grid_intersections(axes);
  ASSERT_EQ(points.size(), 6u);
  for (const Vec3& point : points) {
    EXPECT_FLOAT_EQ(point.y, 0.f);  // 轴网是平面参考，交点恒在 y = 0
  }
  // 6 个点两两不同，且都落在网格范围内。
  for (std::size_t i = 0; i < points.size(); ++i) {
    EXPECT_GE(points[i].x, -1.f);
    EXPECT_LE(points[i].x, 13.f);
    EXPECT_GE(points[i].z, -1.f);
    EXPECT_LE(points[i].z, 6.f);
    for (std::size_t j = i + 1; j < points.size(); ++j) {
      EXPECT_FALSE(points[i].x == points[j].x && points[i].z == points[j].z);
    }
  }
}

TEST(GridIntersections, IdsNarrowTheAxesThatTakePart) {
  // 过一遍 Grid：生成的轴线自己不带 id，按 id 筛选得先有 id。
  Grid grid;
  for (const GridAxis& axis : make_orthogonal_grid(0.0, 0.0, {6.0, 6.0}, {5.0}, 1.0)) {
    grid.add(axis);
  }
  const std::vector<GridAxis>& axes = grid.axes();
  ASSERT_EQ(axes.size(), 5u);  // 3 根编号轴（x = 0/6/12）+ 2 根字母轴（z = 0/5）
  // 只留「第 2 根编号轴」+「第 A 根字母轴」：一个交点（6, 0）。
  const std::vector<std::uint64_t> ids{axes[1].id, axes[3].id};
  const std::vector<Vec3> points = grid_intersections(axes, ids);
  ASSERT_EQ(points.size(), 1u);
  EXPECT_FLOAT_EQ(points[0].x, 6.f);
  EXPECT_FLOAT_EQ(points[0].z, 0.f);

  // 表里已经没有的 id 不算数（删了轴线再布柱不该凭空冒出交点）。
  const std::vector<Vec3> gone = grid_intersections(axes, {9999u});
  EXPECT_TRUE(gone.empty());
}

TEST(GridIntersections, SkipsPointsOutsideTheAxisExtent) {
  std::vector<GridAxis> axes;
  GridAxis numbered;  // 编号轴：x = 0，z 只画到 4
  numbered.name = "1";
  numbered.direction = GridAxisDirection::AlongZ;
  numbered.position = 0.0;
  numbered.start = 0.0;
  numbered.end = 4.0;
  axes.push_back(numbered);
  GridAxis lettered;  // 字母轴：z = 0，x 从 0 到 10
  lettered.name = "A";
  lettered.direction = GridAxisDirection::AlongX;
  lettered.position = 0.0;
  lettered.start = 0.0;
  lettered.end = 10.0;
  axes.push_back(lettered);

  ASSERT_EQ(grid_intersections(axes).size(), 1u);  // (0, 0) 落在两根轴的范围里

  // 字母轴挪到 z = 6：编号轴只画到 z = 4，交点不该出现。
  axes[1].position = 6.0;
  EXPECT_TRUE(grid_intersections(axes).empty());
}

TEST(GridIntersections, CoincidentAxesYieldOnePoint) {
  std::vector<GridAxis> axes;
  GridAxis first;
  first.name = "1";
  first.direction = GridAxisDirection::AlongZ;
  first.position = 3.0;
  first.start = -5.0;
  first.end = 5.0;
  axes.push_back(first);
  axes.push_back(first);  // 重复的轴线：同一个平面点只算一次
  GridAxis lettered;
  lettered.name = "A";
  lettered.direction = GridAxisDirection::AlongX;
  lettered.position = 0.0;
  lettered.start = -5.0;
  lettered.end = 5.0;
  axes.push_back(lettered);

  const std::vector<Vec3> points = grid_intersections(axes);
  ASSERT_EQ(points.size(), 1u);
  EXPECT_FLOAT_EQ(points[0].x, 3.f);
  EXPECT_FLOAT_EQ(points[0].z, 0.f);
}

TEST(GridIntersections, IgnoresDegenerateAxes) {
  std::vector<GridAxis> axes;
  GridAxis numbered;
  numbered.direction = GridAxisDirection::AlongZ;
  numbered.position = 0.0;
  numbered.start = 0.0;
  numbered.end = 0.0;  // 长度 0：不是一根线
  axes.push_back(numbered);
  GridAxis lettered;
  lettered.direction = GridAxisDirection::AlongX;
  lettered.position = 0.0;
  lettered.start = -5.0;
  lettered.end = 5.0;
  axes.push_back(lettered);

  EXPECT_TRUE(grid_intersections(axes).empty());
}

}  // namespace tamias
