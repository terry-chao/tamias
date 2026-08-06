#pragma once

#include "recent_files.h"

#include <QString>
#include <QVector>
#include <QWidget>

class QEvent;
class QLabel;
class QListWidget;
class QWidget;

namespace tamias {

struct OpenDocumentItem {
  QString name;
  QString path;
  int index = -1;
};

class HomePage final : public QWidget {
  Q_OBJECT
 public:
  explicit HomePage(QWidget* parent = nullptr);

  void refresh(const QVector<RecentFileItem>& items);
  void set_open_documents(const QVector<OpenDocumentItem>& items);

 signals:
  void newDemoRequested();
  void openRequested();
  void settingsRequested();
  void fileActivated(const QString& path);
  void missingFileActivated(const QString& path);
  void recentRemoveRequested(const QString& path);
  void openDocumentActivated(int index);

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void apply_theme();
  void rebuild_open_docs();
  void show_recent_menu(const QPoint& pos);

  bool applying_theme_ = false;
  QVector<OpenDocumentItem> open_docs_;
  QWidget* header_ = nullptr;
  QWidget* open_section_ = nullptr;
  QWidget* open_host_ = nullptr;
  QListWidget* recent_list_ = nullptr;
  QWidget* empty_panel_ = nullptr;
  QLabel* empty_label_ = nullptr;
  QLabel* version_label_ = nullptr;
};

}  // namespace tamias
