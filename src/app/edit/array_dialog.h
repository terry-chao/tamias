#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace tamias {

// 阵列参数对话框（线性 / 环形）。只收参数，落盘走 array_entities 命令
// （见 command/edit/copy_entities_command.h）。
class ArrayDialog final : public QDialog {
  Q_OBJECT
 public:
  struct Params {
    bool polar = false;
    int count = 3;            // 含原件
    double spacing = 1.0;     // 线性：相邻间距（米）
    double step_angle = 15.0; // 环形：每份夹角（度）
    double centre_x = 0.0;    // 环形：旋转中心（世界 XZ）
    double centre_z = 0.0;
  };

  // centre_x / centre_z 是环形阵列中心初值（一般给选中范围的中心）。
  explicit ArrayDialog(QWidget* parent = nullptr, double centre_x = 0.0, double centre_z = 0.0);

  [[nodiscard]] Params params() const;

 private:
  void sync_fields();

  QComboBox* mode_ = nullptr;
  QSpinBox* count_ = nullptr;
  QDoubleSpinBox* distance_ = nullptr;
  QLabel* distance_label_ = nullptr;
  QDoubleSpinBox* centre_x_ = nullptr;
  QDoubleSpinBox* centre_z_ = nullptr;
  QLabel* centre_label_ = nullptr;
  QLabel* hint_ = nullptr;
};

}  // namespace tamias
