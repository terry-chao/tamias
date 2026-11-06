#include "app/shell/panel/node_palette_popup.h"

#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace tamias {

namespace {

// 列表项带上的角色：Hit 分数（调试用）之外主要是类型指针。
constexpr int kTypeRole = Qt::UserRole + 1;

QString type_label(const NodeTypeInfo& type) {
  return QString::fromStdString(type.name) + QStringLiteral("  ·  ") +
         QString::fromStdString(type.category);
}

}  // namespace

NodePalettePopup::NodePalettePopup(QWidget* parent) : QWidget(parent, Qt::Popup) {
  setAttribute(Qt::WA_StyledBackground, true);
  setFixedWidth(280);

  edit_ = new QLineEdit(this);
  edit_->setPlaceholderText(tr("Search nodes…"));
  edit_->setClearButtonEnabled(true);

  list_ = new QListWidget(this);
  list_->setUniformItemSizes(true);
  list_->setMinimumHeight(40);
  list_->setMaximumHeight(280);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(4);
  layout->addWidget(edit_);
  layout->addWidget(list_);

  connect(edit_, &QLineEdit::textChanged, this, &NodePalettePopup::refresh_list);
  connect(edit_, &QLineEdit::returnPressed, this, &NodePalettePopup::choose_current);
  // 双击在 Windows 上会先后发 itemDoubleClicked 和 itemActivated，两条都接上
  // 保证哪种风格都能选，真正的去重在 choose_current 的 chosen_ 里。
  connect(list_, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem*) { choose_current(); });
  connect(list_, &QListWidget::itemActivated, this,
          [this](QListWidgetItem*) { choose_current(); });
}

void NodePalettePopup::spawn(const QPoint& global_pos, PortType filter_port, bool as_input) {
  filter_port_ = filter_port;
  as_input_ = as_input;
  chosen_ = false;
  edit_->clear();
  refresh_list();
  move(global_pos);
  show();
  edit_->setFocus();
}

void NodePalettePopup::refresh_list() {
  list_->clear();
  const std::string query = edit_->text().toStdString();
  const std::vector<NodeRegistry::Hit> hits =
      filter_port_ == PortType::Empty
          ? NodeRegistry::builtin().search(query, 30)
          : NodeRegistry::builtin().search_for_port(query, filter_port_, as_input_, 30);
  for (const NodeRegistry::Hit& hit : hits) {
    auto* item = new QListWidgetItem(type_label(*hit.type), list_);
    item->setData(kTypeRole, QVariant::fromValue(reinterpret_cast<void*>(
                                 const_cast<NodeTypeInfo*>(hit.type))));
  }
  if (list_->count() > 0) {
    list_->setCurrentRow(0);
  }
}

void NodePalettePopup::choose_current() {
  // 同一次弹出只认一次选择：双击 / Enter 可能走不止一条信号路径。
  if (chosen_) {
    return;
  }
  QListWidgetItem* current = list_->currentItem();
  if (current == nullptr) {
    hide();
    emit cancelled();
    return;
  }
  chosen_ = true;
  auto* type = reinterpret_cast<NodeTypeInfo*>(current->data(kTypeRole).value<void*>());
  hide();
  if (type != nullptr) {
    emit node_chosen(type);
  }
}

}  // namespace tamias
