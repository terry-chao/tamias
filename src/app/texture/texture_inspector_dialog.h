#pragma once

#include <QDialog>
#include <cstdint>

class QLabel;

namespace tamias {

class Document;

class TextureInspectorDialog final : public QDialog {
  Q_OBJECT
 public:
  TextureInspectorDialog(Document& document, std::uint64_t texture_id, QWidget* parent = nullptr);

  void reload();

 signals:
  void replace_requested();

 private:
  Document* document_ = nullptr;
  std::uint64_t texture_id_ = 0;
  QLabel* preview_ = nullptr;
  QLabel* name_ = nullptr;
  QLabel* usage_ = nullptr;
  QLabel* size_ = nullptr;
  QLabel* color_space_ = nullptr;
  QLabel* refs_ = nullptr;
  QLabel* source_ = nullptr;
  QLabel* used_by_ = nullptr;
};

}  // namespace tamias
