#include "handle_inspector.h"

#include "bim/bim_model.h"
#include "engine/document/document.h"
#include "entity/entity.h"

#include <QClipboard>
#include <QEvent>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QToolTip>
#include <QTimer>
#include <QVBoxLayout>

#include <QRegularExpression>
#include <string>

namespace tamias {
namespace {

QString kind_name(const HandleInspector& self, EntityKind kind) {
  switch (kind) {
    case EntityKind::Wall:
      return self.tr("Wall");
    case EntityKind::Box:
      return self.tr("Box");
    case EntityKind::Cylinder:
      return self.tr("Cylinder");
    case EntityKind::Beam:
      return self.tr("Beam");
    case EntityKind::Column:
      return self.tr("Column");
    case EntityKind::Slab:
      return self.tr("Slab");
    case EntityKind::Door:
      return self.tr("Door");
    case EntityKind::Window:
      return self.tr("Window");
    case EntityKind::StructuralWall:
      return self.tr("Structural Wall");
    case EntityKind::Foundation:
      return self.tr("Foundation");
    case EntityKind::CurtainWall:
      return self.tr("Curtain Wall");
    case EntityKind::Line:
      return self.tr("Line");
    case EntityKind::Polyline:
      return self.tr("Polyline");
    case EntityKind::Circle:
      return self.tr("Circle");
    case EntityKind::Arc:
      return self.tr("Arc");
    case EntityKind::Bezier:
      return self.tr("Bezier");
    case EntityKind::Rectangle:
      return self.tr("Rectangle");
    case EntityKind::BSpline:
      return self.tr("B-spline");
    case EntityKind::Nurbs:
      return self.tr("NURBS");
  }
  return self.tr("Entity");
}

QString relation_kind_name(const HandleInspector& self, RelationKind kind) {
  switch (kind) {
    case RelationKind::HostedOn:
      return self.tr("Hosted on");
  }
  return self.tr("Relation");
}

QString handle_text(std::uint64_t id) {
  return QStringLiteral("%1  (0x%2)").arg(id).arg(id, 0, 16);
}

QString format_relation(const HandleInspector& self, const Relation& rel) {
  return self.tr("id %1  %2  %3 → %4  along=%5  sill=%6  valid=%7")
      .arg(rel.id)
      .arg(relation_kind_name(self, rel.kind))
      .arg(rel.from)
      .arg(rel.to)
      .arg(rel.placement.along, 0, 'f', 3)
      .arg(rel.placement.sill, 0, 'f', 3)
      .arg(rel.valid ? self.tr("yes") : self.tr("no"));
}

// 句柄形如 "1  (0x1)" 时只取括号里的 "0x1"。
QString extract_handle_payload(const QString& text) {
  static const QRegularExpression re(QStringLiteral("\\(0x([0-9A-Fa-f]+)\\)"));
  const QRegularExpressionMatch m = re.match(text);
  return m.hasMatch() ? QStringLiteral("0x%1").arg(m.captured(1)) : text;
}

}  // namespace

HandleInspector::HandleInspector(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  auto* form = new QFormLayout();
  handle_ = new QLabel(this);
  kind_ = new QLabel(this);
  name_ = new QLabel(this);
  mesh_ = new QLabel(this);
  for (auto* label : {handle_, kind_, name_, mesh_}) {
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->installEventFilter(this);
  }
  form->addRow(tr("Handle"), handle_);
  form->addRow(tr("Kind"), kind_);
  form->addRow(tr("Name"), name_);
  form->addRow(tr("Mesh"), mesh_);
  layout->addLayout(form);

  auto* rel_label = new QLabel(tr("Relations"), this);
  relations_ = new QPlainTextEdit(this);
  relations_->setReadOnly(true);
  relations_->setPlaceholderText(tr("No relations"));
  layout->addWidget(rel_label);
  layout->addWidget(relations_, 1);

  // 输入句柄 + 定位：把视口框显到该构件。
  auto* locate_row = new QHBoxLayout();
  handle_input_ = new QLineEdit(this);
  handle_input_->setPlaceholderText(tr("Handle id (decimal or 0x…)"));
  handle_input_->setClearButtonEnabled(true);
  locate_btn_ = new QPushButton(tr("Locate"), this);
  locate_btn_->setToolTip(tr("Frame the viewport on the component with this handle"));
  connect(locate_btn_, &QPushButton::clicked, this, &HandleInspector::emit_locate);
  connect(handle_input_, &QLineEdit::returnPressed, this, &HandleInspector::emit_locate);
  locate_row->addWidget(handle_input_, 1);
  locate_row->addWidget(locate_btn_);
  layout->addLayout(locate_row);

  // 复制提示气泡：底部居中，1 秒后自动消失。
  toast_ = new QLabel(this);
  toast_->setAlignment(Qt::AlignCenter);
  toast_->setStyleSheet(QStringLiteral(
      "background-color: #2d2d2d; color: #ffffff; padding: 6px 12px; border-radius: 4px;"));
  toast_->hide();

  set_empty(tr("Click a component to inspect its document handle"));
}

bool HandleInspector::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::MouseButtonDblClick) {
    if (auto* label = qobject_cast<QLabel*>(watched)) {
      copy_value(label);
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void HandleInspector::copy_value(QLabel* label) {
  if (label == nullptr || toast_ == nullptr) {
    return;
  }
  const QString text = label->text();
  if (text.isEmpty() || text == QStringLiteral("—")) {
    return;
  }
  const QString payload = extract_handle_payload(text);
  QGuiApplication::clipboard()->setText(payload);
  toast_->setText(tr("Copied: %1").arg(payload));
  toast_->adjustSize();
  toast_->move((width() - toast_->width()) / 2, height() - toast_->height() - 10);
  toast_->show();
  toast_->raise();
  QTimer::singleShot(1000, toast_, &QLabel::hide);
}

void HandleInspector::emit_locate() {
  const QString text = handle_input_->text().trimmed();
  if (text.isEmpty()) {
    return;
  }
  bool ok = false;
  // base 0：自动识别 0x 前缀的十六进制和十进制。
  const std::uint64_t id = text.toULongLong(&ok, 0);
  if (!ok || id == 0) {
    handle_input_->setStyleSheet(QStringLiteral("border: 1px solid #c00;"));
    return;
  }
  handle_input_->setStyleSheet(QString());
  emit locate_requested(id);
}

void HandleInspector::set_empty(const QString& note) {
  handle_->setText(QStringLiteral("—"));
  kind_->setText(QStringLiteral("—"));
  name_->setText(QStringLiteral("—"));
  mesh_->setText(QStringLiteral("—"));
  relations_->setPlainText(note);
}

void HandleInspector::show_selection(const Document* document, std::uint64_t node_id) {
  if (document == nullptr || node_id == 0) {
    set_empty(tr("Click a component to inspect its document handle"));
    return;
  }

  const SceneNode* node = document->scene().find(node_id);
  if (node == nullptr) {
    set_empty(tr("No selection"));
    return;
  }

  handle_->setText(handle_text(node->id));
  name_->setText(QString::fromStdString(node->name));
  mesh_->setText(node->mesh_asset_id == 0 ? QStringLiteral("—")
                                         : handle_text(node->mesh_asset_id));

  const Entity* entity = document->entity(node_id);
  if (entity != nullptr) {
    kind_->setText(kind_name(*this, entity->kind()));
  } else {
    kind_->setText(tr("Imported mesh"));
  }

  QStringList lines;
  if (entity != nullptr) {
    if (const Relation* host = document->bim().host_of(entity->id)) {
      lines << format_relation(*this, *host);
    }
    for (const Relation* dep : document->bim().dependents(entity->id)) {
      lines << format_relation(*this, *dep);
    }
  }
  if (lines.isEmpty()) {
    relations_->setPlainText(tr("No relations on this component"));
  } else {
    relations_->setPlainText(lines.join(QLatin1Char('\n')));
  }
}

}  // namespace tamias
