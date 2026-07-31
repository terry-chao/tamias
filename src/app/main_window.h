#pragma once

#include "document/document.h"
#include "document_viewport.h"
#include "home_page.h"
#include "recent_files.h"

#include <QMainWindow>
#include <QStackedWidget>
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
  void frame_all();
  void close_tab(int index);
  void open_recent_path(const QString& path);
  void on_missing_recent(const QString& path);
  void open_settings();
  void show_home();
  void show_documents();
  void activate_open_document(int index);

 private:
  void add_document_tab(std::shared_ptr<Document> document);
  Result<void> populate_document_meshes(Document& document, RenderThread& thread);
  void refresh_home();
  bool open_path(const QString& path);
  int find_open_document(const QString& path) const;
  DocumentViewport* current_viewport() const;

  QStackedWidget* stack_ = nullptr;
  HomePage* home_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  RecentFilesStore recent_;
};

}  // namespace tamias
