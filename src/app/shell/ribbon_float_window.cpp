#include "app/shell/ribbon_float_window.h"

#include "app/shell/ribbon_group.h"

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStyleHints>
#include <QToolButton>
#include <QVBoxLayout>

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

QString float_stylesheet(bool dark) {
  if (dark) {
    return QStringLiteral(
        "#ribbonFloatWindow { background: #313338; border: 1px solid #4a4d51; }"
        "#ribbonFloatHeader { background: #3a3d41; }"
        "#ribbonFloatTitle { color: #d0d0d0; font-size: 11px;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif; }"
        "QToolButton#ribbonFloatDock {"
        "  background: transparent; border: none; border-radius: 3px; color: #c8c8c8;"
        "  font-size: 12px; padding: 0 4px;"
        "}"
        "QToolButton#ribbonFloatDock:hover { background: #50545a; color: #ffffff; }");
  }
  return QStringLiteral(
      "#ribbonFloatWindow { background: #f7f7f7; border: 1px solid #c9c9c9; }"
      "#ribbonFloatHeader { background: #ececec; }"
      "#ribbonFloatTitle { color: #3a3a3a; font-size: 11px;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif; }"
      "QToolButton#ribbonFloatDock {"
      "  background: transparent; border: none; border-radius: 3px; color: #4a4a4a;"
      "  font-size: 12px; padding: 0 4px;"
      "}"
      "QToolButton#ribbonFloatDock:hover { background: #d8d8d8; color: #000000; }");
}

constexpr int kHeaderHeight = 20;

}  // namespace

RibbonFloatWindow::RibbonFloatWindow(RibbonGroup* group, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint), group_(group) {
  setObjectName(QStringLiteral("ribbonFloatWindow"));
  setAttribute(Qt::WA_StyledBackground, true);
  if (group_ != nullptr) {
    setWindowTitle(group_->title());
  }

  root_ = new QVBoxLayout(this);
  root_->setContentsMargins(1, 1, 1, 1);
  root_->setSpacing(0);

  // 标题栏：显示这一组叫什么，同时也是「拖着走 / 拖回 Ribbon」的把手。
  header_ = new QWidget(this);
  header_->setObjectName(QStringLiteral("ribbonFloatHeader"));
  header_->setAttribute(Qt::WA_StyledBackground, true);
  header_->setFixedHeight(kHeaderHeight);
  header_->setCursor(Qt::SizeAllCursor);
  auto* header_layout = new QHBoxLayout(header_);
  header_layout->setContentsMargins(6, 0, 2, 0);
  header_layout->setSpacing(4);

  title_ = new QLabel(group_ != nullptr ? group_->title() : QString(), header_);
  title_->setObjectName(QStringLiteral("ribbonFloatTitle"));
  title_->setCursor(Qt::SizeAllCursor);
  header_layout->addWidget(title_, 1);

  dock_button_ = new QToolButton(header_);
  dock_button_->setObjectName(QStringLiteral("ribbonFloatDock"));
  dock_button_->setText(QStringLiteral("\u00d7"));
  dock_button_->setToolTip(tr("Dock this group back onto the ribbon"));
  dock_button_->setAutoRaise(true);
  dock_button_->setFocusPolicy(Qt::NoFocus);
  dock_button_->setCursor(Qt::PointingHandCursor);
  dock_button_->setFixedSize(16, 16);
  connect(dock_button_, &QToolButton::clicked, this, [this] {
    if (group_ != nullptr) {
      emit dock_requested(group_);
    }
  });
  header_layout->addWidget(dock_button_);

  root_->addWidget(header_);
  if (group_ != nullptr) {
    root_->addWidget(group_);
  }

  if (QStyleHints* hints = QGuiApplication::styleHints()) {
    connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
      apply_theme();
    });
  }
  apply_theme();
  adjustSize();
}

void RibbonFloatWindow::release_group() {
  if (group_ == nullptr) {
    return;
  }
  root_->removeWidget(group_);
  group_ = nullptr;
}

QPoint RibbonFloatWindow::group_offset() const {
  if (group_ == nullptr) {
    return QPoint(0, kHeaderHeight);
  }
  return group_->mapTo(this, QPoint(0, 0));
}

void RibbonFloatWindow::apply_theme() {
  if (applying_theme_) {
    return;
  }
  applying_theme_ = true;
  setStyleSheet(float_stylesheet(is_dark_theme()));
  applying_theme_ = false;
}

void RibbonFloatWindow::changeEvent(QEvent* event) {
  if (!applying_theme_ &&
      (event->type() == QEvent::ThemeChange || event->type() == QEvent::PaletteChange)) {
    apply_theme();
  }
  QWidget::changeEvent(event);
}

void RibbonFloatWindow::closeEvent(QCloseEvent* event) {
  // Alt+F4 / 点系统关窗：别把这一组弄丢了，先收回 Ribbon 再关。
  if (group_ != nullptr) {
    emit dock_requested(group_);
  }
  QWidget::closeEvent(event);
}

void RibbonFloatWindow::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  armed_ = true;
  press_pos_ = event->position().toPoint();
  event->accept();
}

void RibbonFloatWindow::mouseMoveEvent(QMouseEvent* event) {
  if (!armed_ || group_ == nullptr) {
    QWidget::mouseMoveEvent(event);
    return;
  }
  if (!(event->buttons() & Qt::LeftButton)) {
    armed_ = false;
    return;
  }
  if ((event->position().toPoint() - press_pos_).manhattanLength() <
      QApplication::startDragDistance()) {
    event->accept();
    return;
  }
  armed_ = false;
  event->accept();
  // 和拖分组本身走同一条路：落回 Ribbon 就停靠，落在别处就把这个小窗挪过去。
  group_->begin_drag(this, press_pos_);
}

void RibbonFloatWindow::mouseReleaseEvent(QMouseEvent* event) {
  armed_ = false;
  QWidget::mouseReleaseEvent(event);
}

}  // namespace tamias
