#pragma once

#include "engine/render/texture_asset.h"

#include <QWidget>
#include <cstdint>

class QListWidget;
class QPushButton;

namespace tamias {

class Document;

class TextureLibraryPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit TextureLibraryPanel(QWidget* parent = nullptr);

  void set_document(Document* document);

 signals:
  void texture_import_requested(TextureAsset asset);
  void texture_replace_requested(quint64 id, TextureAsset asset);

 private:
  void rebuild();
  void on_import();
  void on_inspect();
  void on_replace(QWidget* dialog_parent = nullptr);
  void update_actions();
  [[nodiscard]] std::uint64_t selected_id() const;

  Document* document_ = nullptr;
  QListWidget* list_ = nullptr;
  QPushButton* import_btn_ = nullptr;
  QPushButton* inspect_btn_ = nullptr;
  QPushButton* replace_btn_ = nullptr;
};

}  // namespace tamias
