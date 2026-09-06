#include "timing_panel.h"

#include "engine/profile/timing_session.h"
#include "engine/profile/timing_xml_writer.h"
#include "timing_timeline_widget.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QDockWidget>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <vector>

namespace tamias {
namespace {

bool is_dark_theme() {
  if (const QStyleHints* hints = QGuiApplication::styleHints()) {
    switch (hints->colorScheme()) {
      case Qt::ColorScheme::Dark:
        return true;
      case Qt::ColorScheme::Light:
        return false;
      case Qt::ColorScheme::Unknown:
        break;
    }
  }
  return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

QString format_elapsed(std::uint64_t us) {
  if (us >= 1'000'000) {
    return QString::number(static_cast<double>(us) / 1'000'000.0, 'f', 2) + QStringLiteral(" s");
  }
  if (us >= 1000) {
    return QString::number(static_cast<double>(us) / 1000.0, 'f', 2) + QStringLiteral(" ms");
  }
  return QString::number(us) + QStringLiteral(" us");
}

QColor category_color(TimingCategory category) {
  switch (category) {
    case TimingCategory::Command:
      return QColor(47, 125, 222);
    case TimingCategory::Modeling:
      return QColor(214, 126, 44);
    case TimingCategory::Render:
      return QColor(46, 160, 110);
    case TimingCategory::Ui:
      return QColor(142, 92, 196);
  }
  return QColor(128, 128, 128);
}

QIcon category_icon(TimingCategory category) {
  QPixmap px(8, 8);
  px.fill(category_color(category));
  return QIcon(px);
}

constexpr int kEventIndexRole = Qt::UserRole;

QTreeWidgetItem* find_event_item(QTreeWidget* tree, int index) {
  if (tree == nullptr || index < 0) {
    return nullptr;
  }
  QTreeWidgetItemIterator it(tree);
  while (*it) {
    if ((*it)->data(0, kEventIndexRole).toInt() == index) {
      return *it;
    }
    ++it;
  }
  return nullptr;
}

QString category_label(const TimingPanel& panel, TimingCategory category) {
  switch (category) {
    case TimingCategory::Command:
      return panel.tr("Command");
    case TimingCategory::Modeling:
      return panel.tr("Modeling");
    case TimingCategory::Render:
      return panel.tr("Render");
    case TimingCategory::Ui:
      return panel.tr("UI");
  }
  return QStringLiteral("?");
}

QString panel_stylesheet(bool dark) {
  const char* chip =
      "QToolButton#timingChipCommand, QToolButton#timingChipModeling, QToolButton#timingChipRender {"
      "  padding: 3px 10px; border-radius: 11px;";
  if (dark) {
    return QStringLiteral(
               "QWidget#timingToolbar { background: #2b2d30; border-bottom: 1px solid #3c3f41; }"
               "QToolButton#timingRecord {"
               "  padding: 4px 14px; border-radius: 14px; font-weight: 600;"
               "  border: 1px solid #e14c4c; color: #e14c4c; background: transparent;"
               "}"
               "QToolButton#timingRecord:hover { background: rgba(225, 76, 76, 40); }"
               "QToolButton#timingRecord:checked {"
               "  background: #c62828; color: #ffffff; border-color: #c62828;"
               "}"
               "QLabel#timingStatus { color: #9aa0a6; font-weight: 600; }"
               "QLabel#timingElapsed { color: #e8eaed; font-weight: 600; font-size: 15px; }"
               "%1"
               "  border: 1px solid #4a4d52; color: #dcdcdc; background: transparent;"
               "}"
               "QToolButton#timingChipCommand:checked { background: rgba(47,125,222,70); border-color: #2f7dde; color: #fff; }"
               "QToolButton#timingChipModeling:checked { background: rgba(214,126,44,70); border-color: #d67e2c; color: #fff; }"
               "QToolButton#timingChipRender:checked { background: rgba(46,160,110,70); border-color: #2ea06e; color: #fff; }"
               "QToolButton#timingTool {"
               "  padding: 4px 10px; border-radius: 4px; border: none; color: #dcdcdc;"
               "  background: transparent;"
               "}"
               "QToolButton#timingTool:hover { background: #3c3f41; }"
               "QToolButton#timingTool:disabled { color: #6b6e74; }")
        .arg(QLatin1String(chip));
  }
  return QStringLiteral(
             "QWidget#timingToolbar { background: #f4f5f7; border-bottom: 1px solid #d9dce1; }"
             "QToolButton#timingRecord {"
             "  padding: 4px 14px; border-radius: 14px; font-weight: 600;"
             "  border: 1px solid #c62828; color: #c62828; background: transparent;"
             "}"
             "QToolButton#timingRecord:hover { background: rgba(198, 40, 40, 24); }"
             "QToolButton#timingRecord:checked {"
             "  background: #c62828; color: #ffffff; border-color: #c62828;"
             "}"
             "QLabel#timingStatus { color: #5f6368; font-weight: 600; }"
             "QLabel#timingElapsed { color: #202124; font-weight: 600; font-size: 15px; }"
             "%1"
             "  border: 1px solid #dadce0; color: #3c4043; background: transparent;"
             "}"
             "QToolButton#timingChipCommand:checked { background: rgba(47,125,222,36); border-color: #2f7dde; }"
             "QToolButton#timingChipModeling:checked { background: rgba(214,126,44,36); border-color: #d67e2c; }"
             "QToolButton#timingChipRender:checked { background: rgba(46,160,110,36); border-color: #2ea06e; }"
             "QToolButton#timingTool {"
             "  padding: 4px 10px; border-radius: 4px; border: none; color: #3c4043;"
             "  background: transparent;"
             "}"
             "QToolButton#timingTool:hover { background: #e8eaed; }"
             "QToolButton#timingTool:disabled { color: #9aa0a6; }")
      .arg(QLatin1String(chip));
}

}  // namespace

TimingPanel::TimingPanel(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* toolbar = new QWidget(this);
  toolbar->setObjectName(QStringLiteral("timingToolbar"));
  auto* bar = new QHBoxLayout(toolbar);
  bar->setContentsMargins(10, 8, 10, 8);
  bar->setSpacing(8);

  record_ = new QToolButton(toolbar);
  record_->setObjectName(QStringLiteral("timingRecord"));
  record_->setCheckable(true);
  record_->setCursor(Qt::PointingHandCursor);
  record_->setFocusPolicy(Qt::NoFocus);
  record_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  record_->setText(tr("Record"));
  record_->setToolTip(tr("Start recording, then stop to inspect the timeline"));

  status_ = new QLabel(tr("Ready"), toolbar);
  status_->setObjectName(QStringLiteral("timingStatus"));
  elapsed_ = new QLabel(QStringLiteral("0.00 s"), toolbar);
  elapsed_->setObjectName(QStringLiteral("timingElapsed"));
  QFont mono = elapsed_->font();
  mono.setFamily(QStringLiteral("Consolas"));
  elapsed_->setFont(mono);

  cat_command_ = make_chip(tr("Command"), QStringLiteral("timingChipCommand"));
  cat_modeling_ = make_chip(tr("Modeling"), QStringLiteral("timingChipModeling"));
  cat_render_ = make_chip(tr("Render"), QStringLiteral("timingChipRender"));
  cat_render_->setToolTip(tr("Include viewport frame submits (noisy while orbiting)"));

  fit_ = make_tool(tr("Fit"));
  fit_->setToolTip(tr("Fit the timeline to this session"));
  clear_ = make_tool(tr("Clear"));
  export_ = make_tool(tr("Export"));
  export_->setToolTip(tr("Save the session as XML"));

  bar->addWidget(record_);
  bar->addWidget(status_);
  bar->addWidget(elapsed_);
  auto* sep = new QFrame(toolbar);
  sep->setFrameShape(QFrame::VLine);
  sep->setFixedWidth(1);
  bar->addWidget(sep);
  bar->addWidget(cat_command_);
  bar->addWidget(cat_modeling_);
  bar->addWidget(cat_render_);
  bar->addStretch(1);
  bar->addWidget(fit_);
  bar->addWidget(clear_);
  bar->addWidget(export_);
  root->addWidget(toolbar);

  splitter_ = new QSplitter(Qt::Vertical, this);
  timeline_ = new TimingTimelineWidget(splitter_);
  tree_ = new QTreeWidget(splitter_);
  tree_->setColumnCount(4);
  tree_->setHeaderLabels({tr("Name"), tr("Total"), tr("Self"), tr("%")});
  tree_->headerItem()->setToolTip(1, tr("Time including nested calls"));
  tree_->headerItem()->setToolTip(2, tr("Time in this scope excluding children"));
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  tree_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setUniformRowHeights(true);
  tree_->setAlternatingRowColors(true);
  tree_->setRootIsDecorated(true);
  tree_->setIndentation(16);
  tree_->setAnimated(false);
  tree_->hide();
  splitter_->setStretchFactor(0, 3);
  splitter_->setStretchFactor(1, 1);
  root->addWidget(splitter_, 1);

  timer_ = new QTimer(this);
  timer_->setInterval(100);

  connect(record_, &QToolButton::clicked, this, [this] { set_recording(record_->isChecked()); });
  connect(clear_, &QToolButton::clicked, this, &TimingPanel::on_clear);
  connect(export_, &QToolButton::clicked, this, &TimingPanel::export_xml);
  connect(fit_, &QToolButton::clicked, this, [this] { timeline_->fit_view(); });
  connect(timer_, &QTimer::timeout, this, &TimingPanel::tick);
  connect(cat_command_, &QToolButton::toggled, this, &TimingPanel::apply_category_checks);
  connect(cat_modeling_, &QToolButton::toggled, this, &TimingPanel::apply_category_checks);
  connect(cat_render_, &QToolButton::toggled, this, &TimingPanel::apply_category_checks);
  connect(timeline_, &TimingTimelineWidget::event_clicked, this, [this](int index) {
    select_tree_event(index);
  });
  connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
    const auto* item = tree_->currentItem();
    if (item == nullptr) {
      timeline_->set_selected(-1);
      return;
    }
    timeline_->set_selected(item->data(0, kEventIndexRole).toInt());
  });

  apply_stylesheet();
  sync_category_checks();
  refresh();
}

QToolButton* TimingPanel::make_chip(const QString& text, const QString& name) {
  auto* chip = new QToolButton(this);
  chip->setObjectName(name);
  chip->setCheckable(true);
  chip->setText(text);
  chip->setCursor(Qt::PointingHandCursor);
  chip->setFocusPolicy(Qt::NoFocus);
  chip->setToolButtonStyle(Qt::ToolButtonTextOnly);
  return chip;
}

QToolButton* TimingPanel::make_tool(const QString& text) {
  auto* button = new QToolButton(this);
  button->setObjectName(QStringLiteral("timingTool"));
  button->setText(text);
  button->setCursor(Qt::PointingHandCursor);
  button->setFocusPolicy(Qt::NoFocus);
  button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  return button;
}

void TimingPanel::apply_stylesheet() { setStyleSheet(panel_stylesheet(is_dark_theme())); }

void TimingPanel::set_recording(bool recording) { apply_recording(recording); }

bool TimingPanel::is_recording() const { return TimingSession::instance().is_recording(); }

void TimingPanel::apply_recording(bool recording) {
  auto& session = TimingSession::instance();
  if (recording && !session.is_recording()) {
    apply_category_checks();
    session.start();
    timer_->start();
  } else if (!recording && session.is_recording()) {
    session.stop();
    timer_->stop();
  }
  {
    QSignalBlocker block(record_);
    record_->setChecked(session.is_recording());
  }
  record_->setText(session.is_recording() ? tr("Stop") : tr("Record"));
  timeline_->set_recording(session.is_recording());
  refresh();
  emit recording_changed(session.is_recording());
}

void TimingPanel::on_clear() {
  TimingSession::instance().clear();
  timer_->stop();
  last_event_count_ = 0;
  {
    QSignalBlocker block(record_);
    record_->setChecked(false);
  }
  record_->setText(tr("Record"));
  timeline_->set_recording(false);
  refresh();
  emit recording_changed(false);
}

void TimingPanel::export_xml() {
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export timing XML"), QString(), tr("Timing report (*.xml);;All files (*.*)"));
  if (path.isEmpty()) {
    return;
  }
#if defined(_WIN32)
  const std::filesystem::path file(path.toStdWString());
#else
  const std::filesystem::path file(path.toStdString());
#endif
  auto result = TimingXmlWriter::write_file(TimingSession::instance(), file);
  if (!result) {
    QMessageBox::warning(this, tr("Export failed"), QString::fromStdString(result.error()));
  }
}

void TimingPanel::tick() {
  update_header();
  const auto count = TimingSession::instance().events().size();
  if (count != last_event_count_) {
    refresh_events();
  }
}

void TimingPanel::sync_category_checks() {
  const auto& session = TimingSession::instance();
  QSignalBlocker b1(cat_command_);
  QSignalBlocker b2(cat_modeling_);
  QSignalBlocker b3(cat_render_);
  cat_command_->setChecked(session.category_enabled(TimingCategory::Command));
  cat_modeling_->setChecked(session.category_enabled(TimingCategory::Modeling));
  cat_render_->setChecked(session.category_enabled(TimingCategory::Render));
}

void TimingPanel::apply_category_checks() {
  auto& session = TimingSession::instance();
  session.set_category_enabled(TimingCategory::Command, cat_command_->isChecked());
  session.set_category_enabled(TimingCategory::Modeling, cat_modeling_->isChecked());
  session.set_category_enabled(TimingCategory::Render, cat_render_->isChecked());
}

void TimingPanel::update_header() {
  auto& session = TimingSession::instance();
  const std::uint64_t elapsed = session.elapsed_us();
  elapsed_->setText(format_elapsed(elapsed));
  if (session.is_recording()) {
    status_->setText(tr("Recording"));
  } else if (elapsed > 0) {
    status_->setText(tr("Stopped"));
  } else {
    status_->setText(tr("Ready"));
  }

  QString title = tr("Timing");
  if (session.is_recording()) {
    title = tr("Timing — Recording %1").arg(format_elapsed(elapsed));
  } else if (elapsed > 0) {
    title = tr("Timing — %1").arg(format_elapsed(elapsed));
  }
  if (auto* dock = qobject_cast<QDockWidget*>(parentWidget())) {
    dock->setWindowTitle(title);
  }
  update_actions();
}

void TimingPanel::update_actions() {
  const bool recording = TimingSession::instance().is_recording();
  const bool has_events = last_event_count_ > 0 || !TimingSession::instance().events().empty();
  fit_->setEnabled(has_events);
  clear_->setEnabled(recording || has_events || TimingSession::instance().elapsed_us() > 0);
  export_->setEnabled(has_events);
}

void TimingPanel::select_tree_event(int index) {
  if (tree_ == nullptr || !tree_->isVisible()) {
    return;
  }
  QTreeWidgetItem* item = find_event_item(tree_, index);
  QSignalBlocker block(tree_);
  tree_->setCurrentItem(item);
  if (item != nullptr) {
    tree_->scrollToItem(item);
  }
}

void TimingPanel::refresh_events() {
  auto& session = TimingSession::instance();
  const auto events = session.events();
  last_event_count_ = events.size();
  const std::uint64_t elapsed = std::max<std::uint64_t>(session.elapsed_us(), 1);
  timeline_->set_session(events, session.elapsed_us() == 0 ? 1 : session.elapsed_us());

  const bool show_tree = !events.empty();
  tree_->setVisible(show_tree);
  if (!show_tree) {
    tree_->clear();
    update_actions();
    return;
  }

  const int selected = timeline_->selected();
  const auto self = exclusive_durations(events);
  const Qt::Alignment num = Qt::AlignRight | Qt::AlignVCenter;

  QSignalBlocker block(tree_);
  tree_->clear();
  std::vector<QTreeWidgetItem*> items(events.size(), nullptr);
  for (int i = 0; i < static_cast<int>(events.size()); ++i) {
    const TimingEvent& event = events[static_cast<std::size_t>(i)];
    auto* item = new QTreeWidgetItem();
    item->setData(0, kEventIndexRole, i);
    item->setIcon(0, category_icon(event.category));
    item->setText(0, QString::fromStdString(event.name));
    item->setToolTip(0, category_label(*this, event.category));
    item->setText(1, format_elapsed(event.duration_us));
    item->setTextAlignment(1, num);
    const std::uint64_t exclusive =
        i < static_cast<int>(self.size()) ? self[static_cast<std::size_t>(i)] : event.duration_us;
    item->setText(2, format_elapsed(exclusive));
    item->setTextAlignment(2, num);
    const double pct =
        100.0 * static_cast<double>(event.duration_us) / static_cast<double>(elapsed);
    item->setText(3, QString::number(pct, 'f', 1));
    item->setTextAlignment(3, num);
    items[static_cast<std::size_t>(i)] = item;
    if (event.parent >= 0 && event.parent < i) {
      items[static_cast<std::size_t>(event.parent)]->addChild(item);
    } else {
      tree_->addTopLevelItem(item);
    }
  }
  tree_->expandAll();
  if (selected >= 0) {
    if (QTreeWidgetItem* item = find_event_item(tree_, selected)) {
      tree_->setCurrentItem(item);
    }
  }
  update_actions();
}

void TimingPanel::refresh() {
  update_header();
  refresh_events();
}

}  // namespace tamias
