#pragma once

#include <QHash>
#include <QPoint>
#include <QString>
#include <QWidget>
#include <array>
#include <vector>

class QFrame;
class QHBoxLayout;

namespace tamias {

enum class RibbonDisplayMode;
class RibbonGroup;

class RibbonPage final : public QWidget {
  Q_OBJECT
 public:
  // 分组在页面上的位置：第几排（0 起）+ 这一排里的第几个。
  struct Slot {
    int row = 0;
    int index = 0;
  };
  // 两排。再多就得重新想页高与「哪一排算满」，先不做。
  static constexpr int kMaxRows = 2;

  explicit RibbonPage(QWidget* parent = nullptr);

  void set_page_id(const QString& id) { page_id_ = id; }
  [[nodiscard]] QString page_id() const { return page_id_; }

  RibbonGroup* add_group(const QString& title);
  RibbonGroup* add_group(const QString& id, const QString& title);
  [[nodiscard]] RibbonGroup* find_group(const QString& id) const;

  void set_display_mode(RibbonDisplayMode mode);
  // 拖动期间把空着的第二排亮出来当落点——不然分组永远进不了第二排。
  void set_drop_target_visible(bool visible);

  // ==== 分组的停靠 / 拖出 ====
  // 把一组工具从这一页上摘下来（浮动窗接管），返回它原来在哪一格。
  [[nodiscard]] Slot detach_group(RibbonGroup* group);
  void insert_group(RibbonGroup* group, Slot slot);
  [[nodiscard]] int group_count() const;
  [[nodiscard]] QWidget* content() const { return content_; }
  // 落点落在哪一格：按 y 选排、按 x 选这一排里插到第几组之前。
  [[nodiscard]] Slot drop_slot_at(const QPoint& content_pos) const;
  void show_drop_indicator(Slot slot);
  void hide_drop_indicator();

 signals:
  void group_added(RibbonGroup* group);

 private:
  // 空排收起、排数变了重算页高。
  void apply_rows();
  void refresh_chrome();
  [[nodiscard]] std::vector<RibbonGroup*> ordered_groups(int row) const;
  [[nodiscard]] int row_of(RibbonGroup* group) const;
  QFrame* ensure_drop_indicator();

  QString page_id_;
  QWidget* content_ = nullptr;
  std::array<QWidget*, kMaxRows> row_hosts_{};
  std::array<QHBoxLayout*, kMaxRows> row_layouts_{};
  QFrame* drop_indicator_ = nullptr;
  QHash<QString, RibbonGroup*> groups_by_id_;
  bool drop_target_visible_ = false;
  int row_height_ = 92;  // 图标 + 文字 92 / 仅图标 54
};

}  // namespace tamias
