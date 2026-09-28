#pragma once

#include "bim/storey.h"

#include <QDialog>

#include <cstdint>
#include <vector>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace tamias {

// 「复制楼层」：选一个源楼层和一组目标楼层（都必须是表里已有的层），确定后由调用方跑
// 一条可撤销的 copy_storey 命令。对话框自己不改文档，只收参数。
class CopyFloorDialog final : public QDialog {
  Q_OBJECT
 public:
  CopyFloorDialog(std::vector<Storey> storeys, std::uint64_t active_storey_id,
                  QWidget* parent = nullptr);

  [[nodiscard]] std::uint64_t source_storey_id() const;
  [[nodiscard]] std::vector<std::uint64_t> target_storey_ids() const;

 public slots:
  void accept() override;

 private:
  void sync_ok_enabled();
  void fill(const std::vector<Storey>& storeys);
  void rebuild_targets();
  [[nodiscard]] std::vector<std::uint64_t> selected_target_storey_ids() const;
  [[nodiscard]] std::uint64_t combo_storey_id(const QComboBox* combo) const;

  std::vector<Storey> storeys_;
  QComboBox* source_ = nullptr;
  QListWidget* targets_ = nullptr;
  QLabel* hint_ = nullptr;
  QLabel* warning_ = nullptr;
  QPushButton* ok_ = nullptr;
};

}  // namespace tamias
