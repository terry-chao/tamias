#pragma once

#include <QWidget>
#include <cstddef>

class QLabel;
class QSplitter;
class QTimer;
class QToolButton;
class QTreeWidget;

namespace tamias {

class TimingTimelineWidget;

class TimingPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit TimingPanel(QWidget* parent = nullptr);

  void set_recording(bool recording);
  [[nodiscard]] bool is_recording() const;
  void export_xml();
  void refresh();

 signals:
  void recording_changed(bool recording);

 private:
  void apply_recording(bool recording);
  void on_clear();
  void tick();
  void sync_category_checks();
  void apply_category_checks();
  void update_header();
  void refresh_events();
  void update_actions();
  void apply_stylesheet();
  void select_tree_event(int index);
  QToolButton* make_chip(const QString& text, const QString& name);
  QToolButton* make_tool(const QString& text);

  QToolButton* record_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* elapsed_ = nullptr;
  QToolButton* cat_command_ = nullptr;
  QToolButton* cat_modeling_ = nullptr;
  QToolButton* cat_render_ = nullptr;
  QToolButton* fit_ = nullptr;
  QToolButton* clear_ = nullptr;
  QToolButton* export_ = nullptr;
  TimingTimelineWidget* timeline_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QSplitter* splitter_ = nullptr;
  QTimer* timer_ = nullptr;
  std::size_t last_event_count_ = 0;
};

}  // namespace tamias
