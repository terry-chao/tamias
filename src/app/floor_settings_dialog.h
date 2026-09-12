#pragma once

#include "bim/storey.h"

#include <QDialog>

#include <cstdint>
#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;

namespace tamias {

// 楼层设置：一张表就是全部楼层——名称 / 标高 / 层高 / 楼层或夹层。
// 对话框只改参数，不自己写文档：确定后由调用方把整张表交给视口跑一条可撤销命令。
class FloorSettingsDialog final : public QDialog {
  Q_OBJECT
 public:
  FloorSettingsDialog(std::vector<Storey> storeys, std::uint64_t active_storey_id,
                      QWidget* parent = nullptr);

  // 定稿后的楼层表（按标高升序）。id == 0 的条目是新建的，由文档分配句柄。
  [[nodiscard]] const std::vector<Storey>& storeys() const { return storeys_; }
  [[nodiscard]] std::uint64_t active_storey_id() const { return active_storey_id_; }

 public slots:
  void accept() override;

 private:
  // at < 0 表示追加到表尾。
  void add_row(const Storey& storey, int at = -1);
  void add_storey_above();      // 在最高一层上面加整层
  void add_mezzanine_above();   // 在选中层与其上一层之间插夹层
  void remove_selected_row();
  // 改层高时，紧贴其上按标高叠放的楼层跟着走（顶标高对齐）。
  void restack_above(int edited_row, double old_elevation, double old_height);
  // 把选中行上面叠放的楼层整体抬高 delta。
  void lift_above(int row, double delta);
  [[nodiscard]] bool collect(QString* error);

  [[nodiscard]] double elevation_at(int row) const;
  [[nodiscard]] double height_at(int row) const;
  [[nodiscard]] bool mezzanine_at(int row) const;
  [[nodiscard]] int row_of_widget(const QWidget* widget) const;
  void set_elevation(int row, double value);
  [[nodiscard]] QString name_at(int row) const;

  QTableWidget* table_ = nullptr;
  QPushButton* add_storey_ = nullptr;
  QPushButton* add_mezzanine_ = nullptr;
  QPushButton* remove_ = nullptr;
  QLabel* hint_ = nullptr;
  std::vector<Storey> storeys_;
  std::uint64_t active_storey_id_ = 0;
  int next_new_index_ = 1;  // 默认新楼层名的序号
};

}  // namespace tamias
