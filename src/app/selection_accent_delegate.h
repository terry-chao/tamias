#pragma once

#include <QStyledItemDelegate>

class QModelIndex;
class QPainter;
class QStyleOptionViewItem;

namespace tamias {

// 调试器列表/表格用：不整行铺选中背景，只在首列左侧画一条小高亮。
class RowAccentDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
};

}  // namespace tamias
