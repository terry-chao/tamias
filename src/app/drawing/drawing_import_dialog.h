#pragma once

#include "bim/drawing_import.h"
#include "engine/drawing/drawing.h"

#include <QDialog>
#include <QString>

#include <cstddef>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace tamias {

// 半自动翻模：选图纸 → 识别 → **人工复核** → 落地。
// 识别是启发式的，所以这一步的重点是那张候选表和勾选，而不是"一键完成"。
class DrawingImportDialog final : public QDialog {
  Q_OBJECT
 public:
  // drawing_path 是当前打开的图纸页路径（可为空，为空就让用户选）。
  // grid 非空时墙端点会吸附到轴网。
  DrawingImportDialog(const QString& drawing_path, const Grid* grid, QWidget* parent = nullptr);

  // 复核后要落地的计划（宿主墙被取消勾选的门窗已一并剔除）。
  [[nodiscard]] const DrawingImportPlan& plan() const { return filtered_; }
  // 被复核剔除的门窗数（落地后要不要提示）。
  [[nodiscard]] std::size_t dropped_openings() const { return dropped_openings_; }

  // 确定时把勾选落实成计划：宿主墙没选上的门窗一并剔除。
  void accept() override;

 private:
  DrawingImportOptions options() const;
  void browse();
  void recognize();
  void rebuild_table();
  void set_all_checked(bool checked);
  void keep_high_confidence();
  void update_summary();
  [[nodiscard]] std::size_t checked_count() const;

  QString path_;
  const Grid* grid_ = nullptr;
  Drawing drawing_;
  bool loaded_ = false;
  DrawingImportPlan plan_;
  DrawingImportPlan filtered_;
  std::size_t dropped_openings_ = 0;

  QLineEdit* path_edit_ = nullptr;
  QComboBox* unit_combo_ = nullptr;
  QLineEdit* wall_layers_ = nullptr;
  QLineEdit* column_layers_ = nullptr;
  QLineEdit* opening_layers_ = nullptr;
  QDoubleSpinBox* wall_thickness_ = nullptr;
  QDoubleSpinBox* wall_height_ = nullptr;
  QDoubleSpinBox* column_height_ = nullptr;
  QDoubleSpinBox* host_tolerance_ = nullptr;
  QCheckBox* snap_to_grid_ = nullptr;
  QTableWidget* table_ = nullptr;
  QPlainTextEdit* warnings_ = nullptr;
  QLabel* summary_ = nullptr;
  QPushButton* ok_button_ = nullptr;
};

}  // namespace tamias
