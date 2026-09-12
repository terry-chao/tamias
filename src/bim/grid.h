#pragma once

#include "bim/grid_axis.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tamias {

// 轴网：定位参考，不是实体构件。一栋建筑一张，随文档存（GRID chunk）。
// 不进退实体表、不进渲染合批；显示由视口走 overlay，见 docs/BIM.md。
class Grid {
 public:
  GridAxis& add(GridAxis axis);     // 分配新 id
  GridAxis& insert(GridAxis axis);  // 保留已有 id（读盘 / redo）
  bool remove(std::uint64_t id);
  void clear();

  [[nodiscard]] GridAxis* find(std::uint64_t id);
  [[nodiscard]] const GridAxis* find(std::uint64_t id) const;

  [[nodiscard]] std::vector<GridAxis>& axes() { return axes_; }
  [[nodiscard]] const std::vector<GridAxis>& axes() const { return axes_; }
  [[nodiscard]] bool empty() const { return axes_.empty(); }
  [[nodiscard]] std::size_t size() const { return axes_.size(); }

  [[nodiscard]] std::uint64_t next_id() const { return next_id_; }
  void set_next_id(std::uint64_t id) { next_id_ = id == 0 ? 1 : id; }

  // 整表替换（轴网设置对话框）：表里已有的 id 保留，id == 0 的按新增分配，并把
  // 分配到的 id **写回调用方的表**——这样 redo 复用同一批句柄，撤销再重做不换 id。
  // 表里没有的即删除。整条命令一步撤销。
  void replace(std::vector<GridAxis>& axes);

  // 按方向 + 位置排序：先编号轴（AlongZ）后字母轴（AlongX），各自从小到大。
  void sort_axes();

  // 所有轴线端点的包围盒（y 恒为 0）；没有轴时 invalid。
  [[nodiscard]] Aabb bounds() const;

  // 渲染用：每根轴 → (起点, 终点) 两两成对。
  void append_segments(std::vector<Vec3>& out) const;

  // 平面点吸附：容差内靠近某根轴就把对应坐标贴到轴上。
  // plan 是 (x, z)；*snapped 回传是否发生了吸附。
  [[nodiscard]] Vec2 snap_plan(Vec2 plan, double tolerance, bool* snapped = nullptr) const;

 private:
  std::vector<GridAxis> axes_;
  std::uint64_t next_id_ = 1;
};

// 由间距表生成正交轴网：编号轴（AlongZ）落在 x = origin_x 起的累计位置上，
// 字母轴（AlongX）落在 z = origin_z 起的累计位置上；轴线两端各外扩 margin（米）。
// 某一侧间距表为空就不生成那一侧的轴。名称按 1,2,3… / A,B,C… 生成。
[[nodiscard]] std::vector<GridAxis> make_orthogonal_grid(
    double origin_x, double origin_z, const std::vector<double>& x_spacings,
    const std::vector<double>& z_spacings, double margin = 1.0);

}  // namespace tamias
