#pragma once

#include "document/document.h"
#include "document_viewport.h"

#include <QMainWindow>
#include <QTabWidget>
#include <memory>

namespace tamias {

class MainWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);

 private slots:
  void open_file();
  void new_demo_document();
  void set_shaded();
  void set_wireframe();
  void frame_all();
  void close_tab(int index);

 private:
  void add_document_tab(std::shared_ptr<Document> document);
  Result<void> populate_document_meshes(Document& document, RenderThread& thread);

  QTabWidget* tabs_ = nullptr;
};

}  // namespace tamias
