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
  void add_empty_axis();
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
  QLabel* place_hint_ = nullptr;
};

}  // namespace tamias
