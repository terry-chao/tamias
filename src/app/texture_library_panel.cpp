#include "texture_library_panel.h"

#include "engine/document/document.h"
#include "texture_image.h"
#include "texture_inspector_dialog.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace tamias {

TextureLibraryPanel::TextureLibraryPanel(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(6);

  list_ = new QListWidget(this);
  list_->setIconSize(QSize(48, 48));
  list_->setSpacing(2);
  root->addWidget(list_, 1);

  auto* row = new QWidget(this);
  auto* buttons = new QHBoxLayout(row);
  buttons->setContentsMargins(0, 0, 0, 0);
  import_btn_ = new QPushButton(tr("Import..."), row);
  inspect_btn_ = new QPushButton(tr("Preview..."), row);
  replace_btn_ = new QPushButton(tr("Replace..."), row);
  buttons->addWidget(import_btn_);
  buttons->addWidget(inspect_btn_);
  buttons->addWidget(replace_btn_);
  buttons->addStretch(1);
  root->addWidget(row);

  connect(import_btn_, &QPushButton::clicked, this, &TextureLibraryPanel::on_import);
  connect(inspect_btn_, &QPushButton::clicked, this, &TextureLibraryPanel::on_inspect);
  connect(replace_btn_, &QPushButton::clicked, this, [this] { on_replace(); });
  connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { on_inspect(); });
  connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem*) { update_actions(); });
  update_actions();
}

void TextureLibraryPanel::set_document(Document* document) {
  document_ = document;
  rebuild();
}

void TextureLibraryPanel::rebuild() {
  const std::uint64_t keep = selected_id();
  list_->clear();
  if (document_ == nullptr) {
    update_actions();
    return;
  }
  std::vector<std::uint64_t> ids;
  ids.reserve(document_->textures().size());
  for (const auto& [id, unused] : document_->textures()) {
    (void)unused;
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  int restore = -1;
  for (const std::uint64_t id : ids) {
    const TextureAsset* tex = document_->texture(id);
    if (tex == nullptr) {
      continue;
    }
    auto* item = new QListWidgetItem(list_);
    item->setData(Qt::UserRole, static_cast<qulonglong>(id));
    const QPixmap thumb = texture_thumbnail(*tex, QSize(48, 48));
    if (!thumb.isNull()) {
      item->setIcon(QIcon(thumb));
    }
    QString name = texture_display_name(*tex);
    if (!tex->builtin_key.empty()) {
      name += tr(" (built-in)");
    }
    item->setText(tr("%1\n%2  %3×%4  refs %5")
                      .arg(name, texture_usage_label(tex->usage))
                      .arg(tex->width)
                      .arg(tex->height)
                      .arg(document_->texture_ref_count(id)));
    if (id == keep) {
      restore = list_->count() - 1;
    }
  }
  if (restore >= 0) {
    list_->setCurrentRow(restore);
  }
  update_actions();
}

void TextureLibraryPanel::update_actions() {
  const bool has = selected_id() != 0;
  inspect_btn_->setEnabled(has);
  replace_btn_->setEnabled(has);
}

void TextureLibraryPanel::on_import() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Import texture"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp)"));
  if (path.isEmpty()) {
    return;
  }
  auto asset = load_texture_image(path, TextureUsage::Unknown, true);
  if (!asset) {
    return;
  }
  emit texture_import_requested(std::move(*asset));
}

void TextureLibraryPanel::on_inspect() {
  const std::uint64_t id = selected_id();
  if (id == 0 || document_ == nullptr) {
    return;
  }
  TextureInspectorDialog dialog(*document_, id, this);
  connect(&dialog, &TextureInspectorDialog::replace_requested, this, [this, &dialog] {
    on_replace(&dialog);
    dialog.reload();
    rebuild();
  });
  dialog.exec();
}

void TextureLibraryPanel::on_replace(QWidget* dialog_parent) {
  const std::uint64_t id = selected_id();
  if (id == 0) {
    return;
  }
  const QString path = QFileDialog::getOpenFileName(
      dialog_parent != nullptr ? dialog_parent : this, tr("Replace texture"), QString(),
      tr("Images (*.png *.jpg *.jpeg *.bmp)"));
  if (path.isEmpty()) {
    return;
  }
  TextureUsage usage = TextureUsage::Unknown;
  bool srgb = true;
  if (const TextureAsset* existing = document_ != nullptr ? document_->texture(id) : nullptr) {
    usage = existing->usage;
    srgb = existing->srgb;
  }
  auto asset = load_texture_image(path, usage, srgb);
  if (!asset) {
    return;
  }
  emit texture_replace_requested(static_cast<quint64>(id), std::move(*asset));
}

std::uint64_t TextureLibraryPanel::selected_id() const {
  QListWidgetItem* item = list_->currentItem();
  if (item == nullptr) {
    return 0;
  }
  return static_cast<std::uint64_t>(item->data(Qt::UserRole).toULongLong());
}

}  // namespace tamias
