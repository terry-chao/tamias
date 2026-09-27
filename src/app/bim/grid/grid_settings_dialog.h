#pragma once

#include "bim/grid.h"

#include <QDialog>

#include <vector>

class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QTableWidget;

namespace tamias {

// 轴网设置：整张轴网表（名称 / 方向 / 位置 / 起点 / 终点）+ 按间距一次生成正交轴网。
// 对话框只负责编辑这张表；确定后由视口接管**放置步骤**（鼠标在模型里点一下落位），
// 落盘走 UpdateGridCommand（一步撤销），见 DocumentViewport。
class GridSettingsDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit GridSettingsDialog(std::vector<GridAxis> axes, QWidget* parent = nullptr);

  // 确定后的整张表（方向 / 位置 / 范围已校验；id 沿用原值，新行为 0）。
  [[nodiscard]] std::vector<GridAxis> axes() const;
  // 是否走"点击放置"（取消勾选 = 直接按表内坐标落位）。
  [[nodiscard]] bool place_with_click() const;
  // 放置锚点：生成行里的原点。视口把它当落位基准——点在哪儿，这个点就落在哪儿。
  [[nodiscard]] Vec2 placement_anchor() const;

 private:
  QComboBox* make_direction_combo(GridAxisDirection direction);
  void add_row(const GridAxis& axis);
  // 新增一根空轴：direction 决定方向，名字按方向取第一个没被占用的（竖轴 1/2/3…、
  // 横轴 A/B/C…）。
  void add_empty_axis(GridAxisDirection direction);
  // 该方向下一个没被占用的轴号（竖轴 = 数字，横轴 = 字母）。
  [[nodiscard]] QString next_axis_name(GridAxisDirection direction) const;
  // 新轴默认落在同方向最后一根轴外侧一个间距处（没有就落在原点），别和已有的重叠。
  [[nodiscard]] double next_axis_position(GridAxisDirection direction) const;
  // 把表里的轴线重新拉成一张完整的正交网（端点按轴网范围算）。勾了自动适配时，
  // 加轴 / 改位置 / 改外扩都会顺手走一遍。
  void fit_axis_extents();
  void maybe_auto_fit();
  // 勾着自动适配时起点 / 终点由轴网范围算出来，就把它俩置灰（不勾才让手填）。
  void update_extent_editability();
  void remove_selected();
  void generate_orthogonal();
  void reload(const std::vector<GridAxis>& axes);

  QTableWidget* table_ = nullptr;
  QLineEdit* x_spacings_ = nullptr;
  QLineEdit* z_spacings_ = nullptr;
  QDoubleSpinBox* origin_x_ = nullptr;
  QDoubleSpinBox* origin_z_ = nullptr;
  QDoubleSpinBox* margin_ = nullptr;
  QCheckBox* place_with_click_ = nullptr;
  QCheckBox* auto_fit_ = nullptr;
  QLabel* place_hint_ = nullptr;
};

}  // namespace tamias
