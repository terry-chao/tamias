#include "texture_inspector_dialog.h"

#include "engine/document/document.h"
#include "texture_image.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

namespace tamias {
namespace {

QString material_name(const std::string& name) {
  if (name == "Default") {
    return QCoreApplication::translate("tamias::PropertyPanel", "Default");
  }
  if (name == "Concrete") {
    return QCoreApplication::translate("tamias::PropertyPanel", "Concrete");
  }
  if (name == "Steel") {
    return QCoreApplication::translate("tamias::PropertyPanel", "Steel");
  }
  if (name == "Glass") {
    return QCoreApplication::translate("tamias::PropertyPanel", "Glass");
  }
  if (name == "Wood") {
    return QCoreApplication::translate("tamias::PropertyPanel", "Wood");
  }
  if (name == "Plaster") {
    return QCoreApplication::translate("tamias::PropertyPanel", "Plaster");
  }
  return name.empty() ? QCoreApplication::translate("tamias::PropertyPanel", "(Custom)")
                      : QString::fromStdString(name);
}

QStringList materials_using(const Document& document, std::uint64_t texture_id) {
  QStringList names;
  for (const auto& [unused, material] : document.materials()) {
    (void)unused;
    if (material.albedo_texture_id == texture_id || material.normal_texture_id == texture_id ||
        material.orm_texture_id == texture_id) {
      names.push_back(material_name(material.name));
    }
  }
  names.sort();
  return names;
}

}  // namespace

TextureInspectorDialog::TextureInspectorDialog(Document& document, std::uint64_t texture_id,
                                               QWidget* parent)
    : QDialog(parent), document_(&document), texture_id_(texture_id) {
  setWindowTitle(tr("Texture"));
  setMinimumSize(560, 360);
  resize(640, 420);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(10);

  auto* body = new QWidget(this);
  auto* row = new QHBoxLayout(body);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(16);

  preview_ = new QLabel(body);
  preview_->setAlignment(Qt::AlignCenter);
  preview_->setMinimumSize(256, 256);
  preview_->setStyleSheet(
      QStringLiteral("background: #2b2d30; border: 1px solid #3c3f41;"));
  row->addWidget(preview_, 1);

  auto* info = new QWidget(body);
  auto* form = new QFormLayout(info);
  form->setContentsMargins(0, 0, 0, 0);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  name_ = new QLabel(info);
  usage_ = new QLabel(info);
  size_ = new QLabel(info);
  color_space_ = new QLabel(info);
  refs_ = new QLabel(info);
  source_ = new QLabel(info);
  source_->setWordWrap(true);
  used_by_ = new QLabel(info);
  used_by_->setWordWrap(true);
  form->addRow(tr("Name"), name_);
  form->addRow(tr("Type"), usage_);
  form->addRow(tr("Size"), size_);
  form->addRow(tr("Color space"), color_space_);
  form->addRow(tr("References"), refs_);
  form->addRow(tr("Source"), source_);
  form->addRow(tr("Used by"), used_by_);
  row->addWidget(info, 1);
  root->addWidget(body, 1);

  auto* buttons = new QDialogButtonBox(this);
  auto* replace = buttons->addButton(tr("Replace..."), QDialogButtonBox::ActionRole);
  buttons->addButton(QDialogButtonBox::Close);
  connect(replace, &QPushButton::clicked, this, &TextureInspectorDialog::replace_requested);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  root->addWidget(buttons);

  reload();
}

void TextureInspectorDialog::reload() {
  if (document_ == nullptr) {
    return;
  }
  const TextureAsset* tex = document_->texture(texture_id_);
  if (tex == nullptr) {
    preview_->clear();
    name_->setText(tr("(missing)"));
    usage_->clear();
    size_->clear();
    color_space_->clear();
    refs_->clear();
    source_->clear();
    used_by_->clear();
    return;
  }

  const QPixmap preview = texture_thumbnail(*tex, QSize(320, 320));
  if (preview.isNull()) {
    preview_->setPixmap(QPixmap());
    preview_->setText(tr("No preview"));
  } else {
    preview_->setText(QString());
    preview_->setPixmap(preview);
  }

  QString name = texture_display_name(*tex);
  if (!tex->builtin_key.empty()) {
    name += tr(" (built-in)");
  }
  name_->setText(name);
  usage_->setText(texture_usage_label(tex->usage));
  size_->setText(tr("%1 × %2").arg(tex->width).arg(tex->height));
  color_space_->setText(tex->srgb ? tr("sRGB") : tr("Linear"));
  refs_->setText(QString::number(document_->texture_ref_count(texture_id_)));
  if (!tex->builtin_key.empty()) {
    source_->setText(tr("Built-in (%1)").arg(QString::fromStdString(tex->builtin_key)));
  } else if (!tex->source_path.empty()) {
    source_->setText(QString::fromStdString(tex->source_path));
  } else {
    source_->setText(tr("Imported"));
  }
  const QStringList users = materials_using(*document_, texture_id_);
  used_by_->setText(users.isEmpty() ? tr("None") : users.join(tr(", ")));
}

}  // namespace tamias
