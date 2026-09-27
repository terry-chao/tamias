#pragma once

#include "bim/storey.h"

#include <QDialog>

#include <cstdint>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;

namespace tamias {

// 「复制楼层」：选源楼层与目标楼层（都必须是表里已有的层），确定后由调用方跑一条
// 可撤销的 copy_storey 命令。对话框自己不改文档，只收参数。
class CopyFloorDialog final : public QDialog {
  Q_OBJECT
 public:
  CopyFloorDialog(std::vector<Storey> storeys, std::uint64_t active_storey_id,
                  QWidget* parent = nullptr);

  [[nodiscard]] std::uint64_t source_storey_id() const;
  [[nodiscard]] std::uint64_t target_storey_id() const;

 public slots:
  void accept() override;

 private:
  void sync_ok_enabled();
  void fill(const std::vector<Storey>& storeys);
  [[nodiscard]] std::uint64_t combo_storey_id(const QComboBox* combo) const;

  QComboBox* source_ = nullptr;
  QComboBox* target_ = nullptr;
  QLabel* hint_ = nullptr;
  QLabel* warning_ = nullptr;
  QPushButton* ok_ = nullptr;
};

}  // namespace tamias
