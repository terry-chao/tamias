#include "handle_inspector.h"

#include "bim/bim_model.h"
#include "engine/document/document.h"
#include "entity/entity.h"

#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QStringList>
#include <QVBoxLayout>

#include <string>

namespace tamias {
namespace {

const char* kind_name(EntityKind kind) {
  switch (kind) {
    case EntityKind::Wall:
      return "Wall";
    case EntityKind::Box:
      return "Box";
    case EntityKind::Cylinder:
      return "Cylinder";
    case EntityKind::Beam:
      return "Beam";
    case EntityKind::Column:
      return "Column";
    case EntityKind::Slab:
      return "Slab";
    case EntityKind::Door:
      return "Door";
    case EntityKind::Window:
      return "Window";
  }
  return "Entity";
}

const char* relation_kind_name(RelationKind kind) {
  switch (kind) {
    case RelationKind::HostedOn:
      return "HostedOn";
  }
  return "Relation";
}

QString handle_text(std::uint64_t id) {
  return QStringLiteral("%1  (0x%2)").arg(id).arg(id, 0, 16);
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
  handle_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  kind_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  name_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  mesh_->setTextInteractionFlags(Qt::TextSelectableByMouse);
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
  set_empty(tr("Click a component to inspect its document handle"));
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
    kind_->setText(QString::fromLatin1(kind_name(entity->kind())));
  } else {
    kind_->setText(tr("Imported mesh"));
  }

  QStringList lines;
  if (entity != nullptr) {
    if (const Relation* host = document->bim().host_of(entity->id)) {
      lines << QStringLiteral("id %1  %2  %3 → %4  along=%5  sill=%6  valid=%7")
                   .arg(host->id)
                   .arg(QString::fromLatin1(relation_kind_name(host->kind)))
                   .arg(host->from)
                   .arg(host->to)
                   .arg(host->placement.along, 0, 'f', 3)
                   .arg(host->placement.sill, 0, 'f', 3)
                   .arg(host->valid ? QStringLiteral("yes") : QStringLiteral("no"));
    }
    for (const Relation* dep : document->bim().dependents(entity->id)) {
      lines << QStringLiteral("id %1  %2  %3 → %4  along=%5  sill=%6  valid=%7")
                   .arg(dep->id)
                   .arg(QString::fromLatin1(relation_kind_name(dep->kind)))
                   .arg(dep->from)
                   .arg(dep->to)
                   .arg(dep->placement.along, 0, 'f', 3)
                   .arg(dep->placement.sill, 0, 'f', 3)
                   .arg(dep->valid ? QStringLiteral("yes") : QStringLiteral("no"));
    }
  }
  if (lines.isEmpty()) {
    relations_->setPlainText(tr("No relations on this component"));
  } else {
    relations_->setPlainText(lines.join(QLatin1Char('\n')));
  }
}

}  // namespace tamias
