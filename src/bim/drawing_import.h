#pragma once

#include "bim/grid.h"
#include "engine/drawing/drawing.h"

#include <cstddef>
#include <string>
#include <vector>

namespace tamias {

// 翻模识别出的**候选**构件：带来源图层与置信度，先给用户看一眼，确认后才落进文档。
// 识别是启发式的，一定会猜错——所以这一层只产出草案，不碰 Document。
struct WallCandidate {
  Vec3 start{};
  Vec3 end{};
  double thickness = 0.2;
  double height = 3.0;
  std::string layer;
  float confidence = 1.0f;
};

struct ColumnCandidate {
  Vec3 position{};      // 柱底中心
  bool circular = true; // 圆柱（图纸上是圆）还是矩形柱
  double width = 0.4;   // 矩形柱截面宽 / 圆柱直径
  double depth = 0.4;   // 矩形柱截面深（圆柱不用）
  double height = 3.0;
  std::string layer;
  float confidence = 1.0f;
};

struct OpeningCandidate {
  bool door = true;
  Vec3 position{};      // 洞口中心（已投影到宿主墙的中线上）
  double width = 0.9;
  double height = 2.1;
  double thickness = 0.05;
  double sill = 0.0;
  std::size_t host_wall = 0;  // walls 的下标；识别不到宿主墙的候选不入表
  std::string layer;
  std::string block;
  float confidence = 1.0f;
};

struct DrawingImportOptions {
  // 图层匹配：逗号分隔、忽略大小写、按「包含」匹配（"墙" 命中 "剪力墙"）。
  std::string wall_layers = "wall,墙";
  std::string column_layers = "column,col,柱";
  std::string opening_layers = "door,window,门,窗";

  double wall_thickness = 0.2;   // 米：单线墙取默认厚度（图纸没画双线时只能给默认值）
  double wall_height = 3.0;
  double column_height = 3.0;
  double column_size = 0.4;      // 截面识别不出来时的默认边长
  double door_width = 0.9;
  double door_height = 2.1;
  double window_width = 1.2;
  double window_height = 1.5;
  double window_sill = 0.9;

  double min_wall_length = 0.30;   // 米：短于此的线段按图例丢弃
  double min_column_size = 0.10;   // 米
  double max_column_size = 3.00;   // 米
  double min_opening_width = 0.40;
  double max_opening_width = 4.00;
  double host_tolerance = 0.35;    // 米：门窗中心到墙中线的最大距离

  double unit_scale = 0.0;         // 0 = 用图纸自带的 $INSUNITS
  double elevation = 0.0;          // 目标标高（米）
  bool align_to_grid = true;       // 墙端点吸附到轴网
  double grid_snap_tolerance = 0.25;
};

struct DrawingImportPlan {
  std::vector<WallCandidate> walls;
  std::vector<ColumnCandidate> columns;
  std::vector<OpeningCandidate> openings;
  std::vector<std::string> warnings;
  std::size_t skipped_segments = 0;

  [[nodiscard]] std::size_t size() const {
    return walls.size() + columns.size() + openings.size();
  }
  [[nodiscard]] bool empty() const { return size() == 0; }
};

// 识别过程用到的图纸级信息（对话框展示与换算说明用）。
struct DrawingImportInfo {
  double unit_scale = 1.0;
  const char* unit_label = "unit";
  std::vector<std::string> layers;
};

// 图纸 → 候选构件。纯 std、不碰文档，可在 tamias_tests 里无头跑。
// grid 非空且 options.align_to_grid 时，墙端点会吸附到轴上（翻模最稳的定位基准）。
[[nodiscard]] DrawingImportPlan build_drawing_import_plan(
    const Drawing& drawing, const DrawingImportOptions& options, const Grid* grid = nullptr,
    DrawingImportInfo* info = nullptr);

// 按用户勾选裁剪候选（人工复核那一步）。宿主墙被去掉的门窗一并丢弃，并把
// host_wall 重映射到新表的下标——否则门窗会挂到另一面墙上。
// keep_* 与 plan 里各表一一对应，长度不足的按"不保留"处理。
[[nodiscard]] DrawingImportPlan filter_drawing_import_plan(
    const DrawingImportPlan& plan, const std::vector<bool>& keep_walls,
    const std::vector<bool>& keep_columns, const std::vector<bool>& keep_openings,
    std::size_t* dropped_openings = nullptr);

}  // namespace tamias
