#include "selection_accent_delegate.h"

#include <QModelIndex>
#include <QPainter>
#include <QStyleOptionViewItem>

namespace tamias {

void RowAccentDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
  QStyleOptionViewItem opt = option;
  const bool selected = opt.state.testFlag(QStyle::State_Selected);
  opt.state &= ~QStyle::State_Selected;

  QStyledItemDelegate::paint(painter, opt, index);

  if (!selected || index.column() != 0 || opt.rect.isEmpty()) {
    return;
  }

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setPen(Qt::NoPen);
  painter->setBrush(opt.palette.highlight());
  constexpr qreal kBarWidth = 3.0;
  constexpr qreal kInset = 2.0;
  const QRectF bar(opt.rect.left() + kInset, opt.rect.top() + kInset, kBarWidth,
                   opt.rect.height() - kInset * 2.0);
  painter->drawRoundedRect(bar, 1.0, 1.0);
  painter->restore();
}

}  // namespace tamias
