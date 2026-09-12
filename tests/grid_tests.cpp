#include "bim/grid.h"
#include "command/command_system.h"
#include "command/update_grid_command.h"
#include "engine/document/document.h"
#include "engine/document/document_io.h"

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

}  // namespace tamias
