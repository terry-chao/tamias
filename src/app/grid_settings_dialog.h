#pragma once

#include "bim/grid.h"

#include <QDialog>

#include <vector>

class QDoubleSpinBox;
class QComboBox;
class QLineEdit;
class QTableWidget;

namespace tamias {

// 轴网设置：整张轴网表（名称 / 方向 / 位置 / 起点 / 终点）+ 按间距一次生成正交轴网。
// 对话框只负责编辑这张表；落盘走 UpdateGridCommand（一步撤销），见 DocumentViewport。
class GridSettingsDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit GridSettingsDialog(std::vector<GridAxis> axes, QWidget* parent = nullptr);

  // 确定后的整张表（方向 / 位置 / 范围已校验；id 沿用原值，新行为 0）。
  [[nodiscard]] std::vector<GridAxis> axes() const;

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
};

}  // namespace tamias
