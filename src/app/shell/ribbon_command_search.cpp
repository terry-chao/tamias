#include "app/shell/ribbon_command_search.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCompleter>
#include <QFontMetrics>
#include <QIcon>
#include <QKeySequence>
#include <QPainter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace tamias {
namespace {

// 结果行的两个自定义角色：出处（画在名字右边）与命令本身（回车 / 单击时用它）。
constexpr int kLocationRole = Qt::UserRole + 1;
// 命令指针按整数存：跨 QCompleter 的代理模型取角色值时，只有内置类型最省心。
constexpr int kActionRole = Qt::UserRole + 2;

constexpr int kRowHeight = 30;
constexpr int kIconSize = 20;
constexpr int kPopupMinWidth = 340;
constexpr int kMaxResults = 40;

// 去掉菜单里的助记符：搜索看的是「另存为」，不是「另存为(&A)」。
QString action_name(const QAction* action) {
  if (action == nullptr) {
    return {};
  }
  QString text = action->text();
  text.remove(QLatin1Char('&'));
  return text.trimmed();
}

// 一条命令能被搜到的全部文字：名字 + 出处 + 提示 + 快捷键。
// 出处与快捷键也算进来是有意的——忘了名字的人常常记得「在视图那一段」或者「Ctrl+S」。
QString entry_haystack(const RibbonCommandEntry& entry) {
  QString hay = action_name(entry.action);
  hay += QLatin1Char('\n');
  hay += entry.location;
  hay += QLatin1Char('\n');
  hay += entry.keywords;
  if (entry.action != nullptr) {
    const QKeySequence shortcut = entry.action->shortcut();
    if (!shortcut.isEmpty()) {
      hay += QLatin1Char(' ');
      hay += shortcut.toString(QKeySequence::NativeText);
    }
  }
  return hay;
}

// 打分：名字上命中排前面（整名 > 前缀 > 出现在名字里），只在出处 / 提示里命中
// 也算命中，但排在后面。有一个词没命中，整条就不算。
int entry_score(const RibbonCommandEntry& entry, const QStringList& tokens) {
  const QString name = action_name(entry.action).toCaseFolded();
  const QString hay = entry_haystack(entry).toCaseFolded();
  int score = 0;
  for (const QString& token : tokens) {
    if (token.isEmpty()) {
      continue;
    }
    if (name == token) {
      score += 100;
    } else if (name.startsWith(token)) {
      score += 40;
    } else if (name.contains(token)) {
      score += 20;
    } else if (hay.contains(token)) {
      score += 5;
    } else {
      return -1;
    }
  }
  // 同样是命中，能点的排在灰着的前面。
  if (entry.action != nullptr && entry.action->isEnabled()) {
    score += 1;
  }
  return score;
}

// 结果行的画法：左边图标、中间名字、右边一小行出处。名字与出处分两号字，
// 光看名字认不出是哪一个「设置」时，出处就是那句「是视图那一段的」。
class RibbonCommandItemDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                               const QModelIndex& index) const override {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    // 行高自己说了算：基类量出来的是「图标 + 缩进」那一套，一行命令不需要那么高。
    // 但要给大号系统字体留出余地，别把字裁了。
    const QFontMetrics metrics(option.font);
    size.setHeight((std::max)(kRowHeight, metrics.height() + 10));
    size.setWidth((std::max)(size.width(), 260));
    return size;
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();  // 名字与出处自己画，不让基类画一行原始的
    const QWidget* widget = opt.widget;
    QStyle* style = widget != nullptr ? widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, widget);

    const bool selected = (opt.state & QStyle::State_Selected) != 0;
    const bool enabled = (opt.state & QStyle::State_Enabled) != 0;
    painter->save();
    QRect row = opt.rect.adjusted(6, 0, -6, 0);

    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    if (!icon.isNull()) {
      const QRect icon_rect(row.left(), row.center().y() - kIconSize / 2, kIconSize, kIconSize);
      icon.paint(painter, icon_rect, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
      row.setLeft(icon_rect.right() + 8);
    }

    const QString name = index.data(Qt::DisplayRole).toString();
    const QString location = index.data(kLocationRole).toString();
    QFont location_font = opt.font;
    location_font.setPointSizeF((std::max)(7.0, opt.font.pointSizeF() - 1.0));
    const QFontMetrics location_metrics(location_font);
    // 出处那一列最多占半行，剩下的宽度全留给名字（名字比出处重要）。
    const int location_width =
        location.isEmpty()
            ? 0
            : (std::min)(location_metrics.horizontalAdvance(location) + 8, row.width() / 2);
    QRect name_rect = row;
    name_rect.setRight(row.right() - location_width);
    const QRect location_rect(row.right() - location_width, row.top(), location_width,
                              row.height());

    QColor name_color =
        opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    QColor location_color =
        opt.palette.color(selected ? QPalette::HighlightedText : QPalette::PlaceholderText);
    if (!enabled) {
      name_color = opt.palette.color(QPalette::Disabled, QPalette::Text);
      location_color = opt.palette.color(QPalette::Disabled, QPalette::Text);
    } else if (!selected) {
      location_color.setAlpha(200);
    }

    const QFontMetrics name_metrics(opt.font);
    painter->setFont(opt.font);
    painter->setPen(name_color);
    painter->drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                      name_metrics.elidedText(name, Qt::ElideRight, name_rect.width()));
    if (location_width > 0) {
      painter->setFont(location_font);
      painter->setPen(location_color);
      painter->drawText(location_rect, Qt::AlignRight | Qt::AlignVCenter,
                        location_metrics.elidedText(location, Qt::ElideRight, location_rect.width()));
    }
    painter->restore();
  }
};

}  // namespace

RibbonCommandSearch::RibbonCommandSearch(QWidget* parent) : QLineEdit(parent) {
  setObjectName(QStringLiteral("ribbonSearch"));
  setPlaceholderText(tr("Search commands"));
  setClearButtonEnabled(true);
  setMinimumWidth(120);
  setMaximumWidth(200);

  model_ = new QStandardItemModel(this);
  completer_ = new QCompleter(model_, this);
  // 「不过滤」是假的不过滤：模型里装的**已经**是匹配结果（见 refresh_results），
  // 让 QCompleter 再按前缀滤一遍只会把「另存」这种中间匹配扔掉。
  completer_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
  completer_->setCompletionColumn(0);
  completer_->setCaseSensitivity(Qt::CaseInsensitive);
  completer_->setModelSorting(QCompleter::UnsortedModel);
  completer_->setWrapAround(false);
  // 一次最多露十来行：再多就出了屏幕，滚动条反而不好使。
  completer_->setMaxVisibleItems(10);
  setCompleter(completer_);

  popup_ = completer_->popup();
  if (popup_ != nullptr) {
    popup_->setObjectName(QStringLiteral("ribbonSearchPopup"));
    // 结果行是我们自己画的（图标 + 名字 + 出处，见上面的 delegate）。QCompleter 默认
    // 装的是它自己那份「一行名字」，所以这里必须换掉。
    popup_->setItemDelegate(new RibbonCommandItemDelegate(popup_));
    popup_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    popup_->setMinimumWidth(kPopupMinWidth);
    popup_->setAlternatingRowColors(false);
  }

  // activated 有两个重载（QModelIndex / QString），选要的那个。
  connect(completer_, qOverload<const QModelIndex&>(&QCompleter::activated), this,
          &RibbonCommandSearch::activate_index);
  connect(this, &QLineEdit::textChanged, this, [this](const QString&) { refresh_results(); });
}

void RibbonCommandSearch::set_entry_provider(EntryProvider provider) {
  provider_ = std::move(provider);
}

void RibbonCommandSearch::set_popup_stylesheet(const QString& sheet) {
  if (popup_ != nullptr) {
    popup_->setStyleSheet(sheet);
  }
}

void RibbonCommandSearch::refresh_results() {
  if (model_ == nullptr || completer_ == nullptr) {
    return;
  }
  model_->clear();
  const QString query = text().trimmed();
  if (query.isEmpty()) {
    if (popup_ != nullptr) {
      popup_->hide();
    }
    return;
  }

  const QStringList tokens = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const std::vector<RibbonCommandEntry> entries =
      provider_ ? provider_() : std::vector<RibbonCommandEntry>{};
  std::vector<std::pair<int, int>> scored;  // {分数, 清单里的下标}
  scored.reserve(entries.size());
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const int score = entry_score(entries[i], tokens);
    if (score >= 0) {
      scored.emplace_back(score, static_cast<int>(i));
    }
  }
  // 同分保持清单本来的顺序（也就是工具带上的先后），stable 不能换成普通排序。
  std::stable_sort(scored.begin(), scored.end(),
                   [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                     return a.first > b.first;
                   });
  if (scored.size() > kMaxResults) {
    scored.resize(kMaxResults);
  }

  for (const auto& [score, index] : scored) {
    const RibbonCommandEntry& entry = entries[static_cast<std::size_t>(index)];
    QAction* action = entry.action;
    auto* item = new QStandardItem(action_name(action));
    item->setEditable(false);
    if (action != nullptr) {
      item->setIcon(action->icon());
      item->setData(entry.location, kLocationRole);
      item->setData(QVariant::fromValue<qulonglong>(
                        static_cast<qulonglong>(reinterpret_cast<quintptr>(action))),
                    kActionRole);
      if (!action->toolTip().isEmpty()) {
        item->setToolTip(action->toolTip());
      }
      // 现在不能用的命令照样列出来（灰着）：找得到、知道它在哪儿，比「搜不着」
      // 更容易解释为什么点不动。
      item->setEnabled(action->isEnabled());
    } else {
      item->setEnabled(false);
    }
    model_->appendRow(item);
  }
  if (model_->rowCount() == 0) {
    auto* none = new QStandardItem(tr("No matching command"));
    none->setEditable(false);
    none->setEnabled(false);
    model_->appendRow(none);
  }

  completer_->setCompletionPrefix(query);
  completer_->complete();
  if (popup_ != nullptr) {
    popup_->setMinimumWidth((std::max)(kPopupMinWidth, width() * 2));
  }
}

void RibbonCommandSearch::activate_index(const QModelIndex& index) {
  if (!index.isValid()) {
    return;
  }
  const qulonglong raw = index.data(kActionRole).toULongLong();
  auto* action = raw == 0 ? nullptr
                          : reinterpret_cast<QAction*>(static_cast<quintptr>(raw));
  clear();  // 命令已经发出去了，输入框回到待命状态（顺带把结果列表收起来）
  if (action == nullptr || !action->isEnabled()) {
    return;
  }
  action->trigger();
  // QCompleter 会把被激活那一行的名字再写回输入框：等它写完，我们再说一次「清空」。
  QMetaObject::invokeMethod(this, [this] { clear(); }, Qt::QueuedConnection);
}

}  // namespace tamias
