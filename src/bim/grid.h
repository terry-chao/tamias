#pragma once

#include "bim/grid_axis.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tamias {

// 轴网所在的标高（米）：轴网是**地面 / 平面定位参考**，数据恒在 y = 0 上。
// 视口按它画 / 点，轴网上的操作（如轴网布柱）也以它为基准，不跟楼层标高走——
// 不然轴网和摆在它上面的东西会各自飘到一个高度，对不上（见 docs/BIM.md）。
inline constexpr double kGridPlaneY = 0.0;

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

  // ==== 选择（编辑器状态，不落盘；和 SceneNode.selected 同理）====
  void select(std::uint64_t id);
  void deselect(std::uint64_t id);
  void clear_selection();
  [[nodiscard]] bool axis_selected(std::uint64_t id) const;
  [[nodiscard]] bool has_selection() const;
  [[nodiscard]] std::vector<std::uint64_t> selected_ids() const;

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

// 一组轴线 → (起点, 终点) 两两成对（长度为 0 的轴跳过）。Grid 与放置预览共用。
void append_axis_segments(const std::vector<GridAxis>& axes, std::vector<Vec3>& out);

// 轴交点：编号轴（AlongZ）× 字母轴（AlongX）的笛卡尔积 —— 「轴交布置」的几何底子。
// ids 为空就用整张表；给了 id 就只认表里的这些轴（表里已经没有的 id 跳过）。
// 交点必须同时落在两根轴**自己的范围**里（留 tolerance 的余量），一根画短了的轴
// 不会把柱子带到它没画到的地方。同一个平面点（tolerance 内）只出一次，重复的轴线
// 不会叠出两根柱子。
// y 恒为 0：轴网是平面参考，抬到哪一层的标高由调用方决定（见
// command/create/component/create_columns_on_grid_command.h）。
[[nodiscard]] std::vector<Vec3> grid_intersections(const std::vector<GridAxis>& axes,
                                                   const std::vector<std::uint64_t>& ids = {},
                                                   double tolerance = 1e-6);

// 轴号命名：0 → A、25 → Z、26 → AA（超过 26 根字母轴时的进位）。
[[nodiscard]] std::string grid_axis_letter_name(int index);

// 放置 / 落点吸附：光标平面点附近有**已存在的轴网交点**就贴到最近的那个交点上
// （编号轴 × 字母轴的笛卡尔积，且两轴都覆盖这个点，见 grid_intersections）；
// 没命中交点时退而求其次，把 x 贴最近的竖轴、z 贴最近的横轴（各自在容差内），
// 这样只有单向轴时也能对齐到轴线。tolerance 是平面距离（米）。
struct GridSnap {
  Vec2 point;                   // 吸附后的平面点（没有吸附时 = 输入的 plan）
  bool snapped = false;         // 是否发生了吸附（交点或单轴）
  bool on_intersection = false; // 是否吸附到了交点（决定标记的样式）
};
[[nodiscard]] GridSnap snap_plan_to_grid(const std::vector<GridAxis>& axes, Vec2 plan,
                                         double tolerance);

// 放置（整张轴网平移）：轴的方向不变，固定坐标和沿轴两端一起挪。
// dx 落在 X 方向、dz 落在 Z 方向——编号轴的 position 是 x、字母轴的 position 是 z，
// 端点跟着各自的方向挪，别把 start/end 当成同一种坐标。
void translate_grid(std::vector<GridAxis>& axes, double dx, double dz);

// 把一组轴线拉成一张完整的正交网（正交汇）：每根轴都跨到「轴网范围」的两端，各外扩
// margin（米），于是任意竖轴 × 横轴都真的相交，而不是各自一截短线段。
// 范围怎么定：x 范围 = 所有竖轴（AlongZ）的 position 两端，z 范围 = 所有横轴（AlongX）
// 的 position 两端。某一方向只有一根轴（或一根都没有）时范围会退化成一个点，就按
// min_span 在它两侧对称撑开，免得轴线被拉成零长。
void fit_grid_axes_to_extent(std::vector<GridAxis>& axes, double margin = 1.0,
                             double min_span = 20.0);

}  // namespace tamias
