#include "ribbon_page.h"

#include "ribbon_group.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSizePolicy>
#include <QWidget>
#include <QVBoxLayout>

namespace tamias {

RibbonPage::RibbonPage(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("ribbonPage"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFixedHeight(92);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("ribbonPageScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setFocusPolicy(Qt::NoFocus);

  auto* content = new QWidget(scroll);
  content->setObjectName(QStringLiteral("ribbonPageContent"));
  groups_layout_ = new QHBoxLayout(content);
  groups_layout_->setContentsMargins(4, 0, 8, 0);
  groups_layout_->setSpacing(0);
  groups_layout_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  groups_layout_->addStretch(1);
  scroll->setWidget(content);

  root->addWidget(scroll);
}

RibbonGroup* RibbonPage::add_group(const QString& title) {
  if (last_group_) {
    last_group_->set_separator_visible(true);
  }
  auto* group = new RibbonGroup(title, this);
  group->set_separator_visible(false);
  groups_layout_->insertWidget(groups_layout_->count() - 1, group);
  last_group_ = group;
  return group;
}

}  // namespace tamias
