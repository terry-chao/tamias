#pragma once

#include "document/document.h"
#include "document_viewport.h"

#include <QAction>
#include <QActionGroup>
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
  void set_render_mode(RenderMode mode);
  void frame_all();
  void close_tab(int index);
  void on_tab_changed(int index);

 private:
  void add_document_tab(std::shared_ptr<Document> document);
  Result<void> populate_document_meshes(Document& document, RenderThread& thread);
  void sync_render_mode_actions();
  DocumentViewport* current_viewport() const;

  QTabWidget* tabs_ = nullptr;
  QAction* wireframe_action_ = nullptr;
  QAction* shaded_action_ = nullptr;
  QAction* realistic_action_ = nullptr;
  QActionGroup* render_mode_group_ = nullptr;
};

}  // namespace tamias
