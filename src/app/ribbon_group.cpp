#include "ribbon_group.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSize>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace tamias {

RibbonGroup::RibbonGroup(const QString& title, QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("ribbonGroup"));
  setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(8, 6, 0, 4);
  root->setSpacing(8);

  auto* body = new QWidget(this);
  auto* body_layout = new QVBoxLayout(body);
  body_layout->setContentsMargins(0, 0, 0, 0);
  body_layout->setSpacing(2);

  auto* buttons = new QWidget(body);
  buttons_layout_ = new QHBoxLayout(buttons);
  buttons_layout_->setContentsMargins(0, 0, 0, 0);
  buttons_layout_->setSpacing(2);
  buttons_layout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  body_layout->addWidget(buttons, 1);

  title_ = new QLabel(title, body);
  title_->setObjectName(QStringLiteral("ribbonGroupTitle"));
  title_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  body_layout->addWidget(title_);

  separator_ = new QFrame(this);
  separator_->setObjectName(QStringLiteral("ribbonGroupSep"));
  separator_->setFrameShape(QFrame::VLine);
  separator_->setFixedWidth(1);

  root->addWidget(body);
  root->addWidget(separator_);
}

QToolButton* RibbonGroup::add_action(QAction* action) {
  auto* button = new QToolButton(this);
  button->setObjectName(QStringLiteral("ribbonButton"));
  button->setDefaultAction(action);
  button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  button->setIconSize(QSize(28, 28));
  button->setAutoRaise(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setCursor(Qt::PointingHandCursor);
  button->setMinimumWidth(52);
  button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
  buttons_layout_->addWidget(button, 0, Qt::AlignTop);
  return button;
}

void RibbonGroup::set_separator_visible(bool visible) {
  separator_->setVisible(visible);
}

}  // namespace tamias
