#pragma once

#include "engine/drawing/drawing_layer.h"
#include "engine/drawing/drawing_path.h"
#include "engine/drawing/drawing_text.h"
#include "engine/math/aabb2.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tamias {

// 一张二维图纸（当前只有 DXF 会产生它）。引擎侧、Qt-free：
// 解析产出数据，画由 app 层用 QPainter 完成。
//
// 坐标：右手 2D，Y 向上，单位 = 图纸单位（DXF 不保证单位，按原样）。
class Drawing {
 public:
  void add_path(DrawingPath path);
  void add_text(DrawingText text);

  // 取图层索引；不存在则按 name/color 建一层。返回索引。
  [[nodiscard]] std::uint32_t ensure_layer(std::string_view name, Vec3 color);

  // 把整体平移成靠近原点（dxf 常用大坐标，float 精度会掉），
  // 被减掉的量记在 world_origin()，显示绝对坐标时加回去。
  void normalize_origin();

  [[nodiscard]] const std::vector<DrawingPath>& paths() const { return paths_; }
  [[nodiscard]] const std::vector<DrawingText>& texts() const { return texts_; }
  [[nodiscard]] const std::vector<DrawingLayer>& layers() const { return layers_; }
  [[nodiscard]] const Aabb2& bounds() const { return bounds_; }
  [[nodiscard]] Vec2 world_origin() const { return world_origin_; }
  [[nodiscard]] bool empty() const { return paths_.empty() && texts_.empty(); }

  // 解析时跳过的图元数（SPLINE / HATCH / 三维实体等未支持的实体）。
  [[nodiscard]] std::size_t unsupported_entity_count() const { return unsupported_; }
  void add_unsupported_entity() { ++unsupported_; }

  // DXF 的 $INSUNITS（AutoCAD 图纸单位编号：1=英寸、4=毫米、5=厘米、6=米…）。
  // 0 = 没写。翻模必须知道它，否则 12000（毫米）会被当成 12000 米。
  void set_insunits(int units) { insunits_ = units; }
  [[nodiscard]] int insunits() const { return insunits_; }
  // 图纸单位 → 米。未知单位按 1（当作米）——调用方可以用导入选项覆盖。
  [[nodiscard]] double unit_scale_to_meter() const;
  // 单位可读名（"mm" / "m" / 没写时为 "unit"）。
  [[nodiscard]] const char* unit_label() const;

 private:
  void expand_bounds(Vec2 p);

  std::vector<DrawingPath> paths_;
  std::vector<DrawingText> texts_;
  std::vector<DrawingLayer> layers_;
  std::unordered_map<std::string, std::uint32_t> layer_lookup_;
  Aabb2 bounds_{};
  Vec2 world_origin_{};
  std::size_t unsupported_ = 0;
  int insunits_ = 0;
};

}  // namespace tamias
