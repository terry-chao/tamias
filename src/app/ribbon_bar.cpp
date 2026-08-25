#include "ribbon_bar.h"

#include "ribbon_page.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QSize>
#include <QSizePolicy>
#include <QStackedWidget>
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

QString ribbon_stylesheet(bool dark) {
  if (dark) {
    return QStringLiteral(
        "#ribbonBar { background: #2b2d30; }"
        "#ribbonTabRow {"
        "  background: #2b2d30; border-bottom: 1px solid #3c3f41;"
        "}"
        "#ribbonPages, #ribbonPage, #ribbonPageContent, #ribbonPageScroll {"
        "  background: #313338;"
        "}"
        "QToolButton#ribbonTab {"
        "  background: transparent; border: none; border-bottom: 3px solid transparent;"
        "  color: #dcdcdc; padding: 8px 14px 6px 14px; font-size: 13px;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "QToolButton#ribbonTab:checked {"
        "  color: #6cb6ff; border-bottom: 3px solid #6cb6ff; font-weight: 600;"
        "}"
        "QToolButton#ribbonTab:hover { color: #ffffff; }"
        "QToolButton#ribbonQuickButton, QToolButton#ribbonCollapse {"
        "  background: transparent; border: none; border-radius: 4px; padding: 4px;"
        "}"
        "QToolButton#ribbonQuickButton:hover, QToolButton#ribbonCollapse:hover {"
        "  background: #3c3f41;"
        "}"
        "QToolButton#ribbonButton {"
        "  background: transparent; border: none; border-radius: 4px;"
        "  color: #dcdcdc; padding: 4px 8px 2px 8px; font-size: 11px;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "QToolButton#ribbonButton:hover { background: #3c3f41; }"
        "QToolButton#ribbonButton:checked, QToolButton#ribbonButton:pressed {"
        "  background: #45494b;"
        "}"
        "#ribbonGroupTitle {"
        "  color: #8c8c8c; font-size: 11px;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "#ribbonGroupSep { background: #3c3f41; border: none; max-width: 1px; }"
        "QScrollArea#ribbonPageScroll { background: transparent; border: none; }");
  }

  return QStringLiteral(
      "#ribbonBar { background: #f7f7f7; }"
      "#ribbonTabRow {"
      "  background: #ffffff; border-bottom: 1px solid #e6e6e6;"
      "}"
      "#ribbonPages, #ribbonPage, #ribbonPageContent, #ribbonPageScroll {"
      "  background: #f7f7f7;"
      "}"
      "QToolButton#ribbonTab {"
      "  background: transparent; border: none; border-bottom: 3px solid transparent;"
      "  color: #222222; padding: 8px 14px 6px 14px; font-size: 13px;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "QToolButton#ribbonTab:checked {"
      "  color: #1a73e8; border-bottom: 3px solid #1a73e8; font-weight: 600;"
      "}"
      "QToolButton#ribbonTab:hover { color: #1a73e8; }"
      "QToolButton#ribbonQuickButton, QToolButton#ribbonCollapse {"
      "  background: transparent; border: none; border-radius: 4px; padding: 4px;"
      "}"
      "QToolButton#ribbonQuickButton:hover, QToolButton#ribbonCollapse:hover {"
      "  background: #ececec;"
      "}"
      "QToolButton#ribbonButton {"
      "  background: transparent; border: none; border-radius: 4px;"
      "  color: #333333; padding: 4px 8px 2px 8px; font-size: 11px;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "QToolButton#ribbonButton:hover { background: #e8e8e8; }"
      "QToolButton#ribbonButton:checked, QToolButton#ribbonButton:pressed {"
      "  background: #dadada;"
      "}"
      "#ribbonGroupTitle {"
      "  color: #6a6a6a; font-size: 11px;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "#ribbonGroupSep { background: #d8d8d8; border: none; max-width: 1px; }"
      "QScrollArea#ribbonPageScroll { background: transparent; border: none; }");
}

}  // namespace

RibbonBar::RibbonBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("ribbonBar"));
  setAttribute(Qt::WA_StyledBackground, true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  tab_row_ = new QWidget(this);
  tab_row_->setObjectName(QStringLiteral("ribbonTabRow"));
  tab_row_->setFixedHeight(36);
  auto* tabs = new QHBoxLayout(tab_row_);
  tabs->setContentsMargins(10, 0, 8, 0);
  tabs->setSpacing(2);

  auto* logo = new QLabel(tab_row_);
  logo->setFixedSize(22, 22);
  logo->setAlignment(Qt::AlignCenter);
  const QPixmap brand(QStringLiteral(":/branding/logo.png"));
  if (!brand.isNull()) {
    logo->setPixmap(brand.scaled(22, 22, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
  tabs->addWidget(logo, 0, Qt::AlignVCenter);
  tabs->addSpacing(6);

  auto* quick_host = new QWidget(tab_row_);
  quick_layout_ = new QHBoxLayout(quick_host);
  quick_layout_->setContentsMargins(0, 0, 0, 0);
  quick_layout_->setSpacing(0);
  tabs->addWidget(quick_host, 0, Qt::AlignVCenter);
  tabs->addSpacing(8);

  tab_buttons_layout_ = new QHBoxLayout();
  tab_buttons_layout_->setContentsMargins(0, 0, 0, 0);
  tab_buttons_layout_->setSpacing(0);
  tabs->addLayout(tab_buttons_layout_);
  tabs->addStretch(1);

  collapse_button_ = new QToolButton(tab_row_);
  collapse_button_->setObjectName(QStringLiteral("ribbonCollapse"));
  collapse_button_->setAutoRaise(true);
  collapse_button_->setFocusPolicy(Qt::NoFocus);
  collapse_button_->setCursor(Qt::PointingHandCursor);
  collapse_button_->setIconSize(QSize(12, 12));
  connect(collapse_button_, &QToolButton::clicked, this, &RibbonBar::toggle_collapsed);
  tabs->addWidget(collapse_button_, 0, Qt::AlignVCenter);

  tab_group_ = new QButtonGroup(this);
  tab_group_->setExclusive(true);

  pages_ = new QStackedWidget(this);
  pages_->setObjectName(QStringLiteral("ribbonPages"));

  root->addWidget(tab_row_);
  root->addWidget(pages_);

  connect(tab_group_, &QButtonGroup::idClicked, this, [this](int id) {
    if (id >= 0) {
      pages_->setCurrentIndex(id);
      if (collapsed_) {
        set_collapsed(false);
      }
    }
  });

  if (QStyleHints* hints = QGuiApplication::styleHints()) {
    connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
      apply_theme();
    });
  }

  update_collapse_button();
  apply_theme();
}

void RibbonBar::add_quick_action(QAction* action) {
  auto* button = new QToolButton(this);
  button->setObjectName(QStringLiteral("ribbonQuickButton"));
  button->setDefaultAction(action);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setIconSize(QSize(16, 16));
  button->setAutoRaise(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setCursor(Qt::PointingHandCursor);
  quick_layout_->addWidget(button);
}

RibbonPage* RibbonBar::add_page(const QString& title) {
  return add_page(title.trimmed().toCaseFolded(), title);
}

RibbonPage* RibbonBar::add_page(const QString& id, const QString& title) {
  if (RibbonPage* existing = find_page(id)) {
    return existing;
  }

  auto* page = new RibbonPage(pages_);
  const int index = pages_->addWidget(page);
  pages_by_id_.insert(id, page);

  auto* tab = new QToolButton(tab_row_);
  tab->setObjectName(QStringLiteral("ribbonTab"));
  tab->setText(title);
  tab->setCheckable(true);
  tab->setAutoRaise(true);
  tab->setFocusPolicy(Qt::NoFocus);
  tab->setCursor(Qt::PointingHandCursor);
  tab_group_->addButton(tab, index);
  tab_buttons_layout_->addWidget(tab);
  if (index == 0) {
    tab->setChecked(true);
    pages_->setCurrentIndex(0);
  }
  return page;
}

RibbonPage* RibbonBar::find_page(const QString& id) const { return pages_by_id_.value(id); }

void RibbonBar::set_collapsed(bool collapsed) {
  if (collapsed_ == collapsed) {
    return;
  }
  collapsed_ = collapsed;
  pages_->setVisible(!collapsed_);
  update_collapse_button();
  updateGeometry();
}

void RibbonBar::toggle_collapsed() { set_collapsed(!collapsed_); }

void RibbonBar::update_collapse_button() {
  collapse_button_->setArrowType(collapsed_ ? Qt::DownArrow : Qt::UpArrow);
  collapse_button_->setToolTip(collapsed_ ? tr("Expand the ribbon")
                                          : tr("Collapse the ribbon"));
}

void RibbonBar::apply_theme() {
  if (applying_theme_) {
    return;
  }
  applying_theme_ = true;
  setStyleSheet(ribbon_stylesheet(is_dark_theme()));
  applying_theme_ = false;
}

void RibbonBar::changeEvent(QEvent* event) {
  if (!applying_theme_ &&
      (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange)) {
    apply_theme();
  }
  QWidget::changeEvent(event);
}

}  // namespace tamias
