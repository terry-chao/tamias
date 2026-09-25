#pragma once

#include <QHash>
#include <QPoint>
#include <QString>
#include <QWidget>
#include <vector>

class QFrame;
class QHBoxLayout;
class QVBoxLayout;

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
  // 排数动态：拖动时在末尾多亮一条空排当落点，放下就多一排；空了就收。
  // 这个上限只是防手滑拖出十几排，不是设计约束。
  static constexpr int kMaxRows = 6;

  explicit RibbonPage(QWidget* parent = nullptr);

  void set_page_id(const QString& id) { page_id_ = id; }
  [[nodiscard]] QString page_id() const { return page_id_; }

  RibbonGroup* add_group(const QString& title);
  RibbonGroup* add_group(const QString& id, const QString& title);
  [[nodiscard]] RibbonGroup* find_group(const QString& id) const;

  void set_display_mode(RibbonDisplayMode mode);
  // 拖动期间把末尾那条空排亮出来当落点——不然分组永远进不了新的一排。
  void set_drop_target_visible(bool visible);

  // 页高必须走 sizeHint 上报：只 setFixedHeight 的话固定高度不进 sizeHint，
  // QStackedWidget / QMainWindow 会一直按第一排的高度给 Ribbon 留地方，
  // 多出来的那排就落在工具栏矩形外面，鼠标根本够不到。
  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

  // ==== 分组的停靠 / 拖出 ====
  // 把一组工具从这一页上摘下来（浮动窗接管），返回它原来在哪一格。
  [[nodiscard]] Slot detach_group(RibbonGroup* group);
  void insert_group(RibbonGroup* group, Slot slot);
  [[nodiscard]] int group_count() const;
  [[nodiscard]] QWidget* content() const { return content_; }
  // 这一组现在挂在哪一排（-1 = 没挂在页面上：浮动出去了，或刚被摘下来）。
  [[nodiscard]] int row_of(RibbonGroup* group) const;

  // ==== 布局记忆（见 ribbon_bar.h 的 layout_keys / apply_layout）====
  // 挂在页面上的一组工具 + 它占的格子。
  struct Placement {
    RibbonGroup* group = nullptr;
    Slot slot{};
  };
  // 现在还挂在页面上的分组，按排 → 排内序号（浮动出去的不在内）。
  [[nodiscard]] std::vector<Placement> placements() const;
  // 这一页登记的全部分组（含浮动出去的）：回放时要靠它把「记录里没有的组」补回来。
  [[nodiscard]] std::vector<RibbonGroup*> all_groups() const;
  // 回放前把这一页的分组一次摘光：只有全空着，insert_group 的「自己原来在左边」
  // 修正才不会把回放顺序搞乱。
  void detach_all_groups();
  // 补到最后一排末尾（记录里没有这一组时用），新版本新增的分组不会因为旧布局消失。
  void append_group(RibbonGroup* group);
  // 落点落在哪一格：按 y 选排、按 x 选这一排里插到第几组之前。
  [[nodiscard]] Slot drop_slot_at(const QPoint& content_pos) const;
  void show_drop_indicator(Slot slot);
  void hide_drop_indicator();

 signals:
  void group_added(RibbonGroup* group);
  // 可见排数（或单排高度）变了：RibbonBar 收到后要把新高度转告 QMainWindow。
  void rows_changed();

 private:
  // 需要几排就建几排（只加不减，删排没必要）。
  void ensure_rows(int count);
  // 有内容的排数（最后一排有东西的那一排 + 1，至少 1）。
  [[nodiscard]] int rows_with_content() const;
  // 去掉中间的空排：后面的排整体上移，别在页面上留一条空白带。
  void compact_rows();
  // 空排收起、排数变了重算页高。
  void apply_rows();
  void refresh_chrome();
  [[nodiscard]] std::vector<RibbonGroup*> ordered_groups(int row) const;
  QFrame* ensure_drop_indicator();

  QString page_id_;
  QWidget* content_ = nullptr;
  QVBoxLayout* rows_layout_ = nullptr;
  std::vector<QWidget*> row_hosts_;
  std::vector<QHBoxLayout*> row_layouts_;
  QFrame* drop_indicator_ = nullptr;
  QHash<QString, RibbonGroup*> groups_by_id_;
  bool drop_target_visible_ = false;
  int visible_rows_ = 1;
  int applied_height_ = 0;  // 上一次真正应用给页面的高度，用来判断要不要通知外层
  int row_height_ = 92;  // 图标 + 文字 92 / 仅图标 54
};

}  // namespace tamias
