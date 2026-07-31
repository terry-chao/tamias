#pragma once

#include "recent_files.h"

#include <QString>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;
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

 private:
  void clear_layout(QGridLayout* layout);
  void clear_cards();

  QWidget* open_section_ = nullptr;
  QWidget* open_host_ = nullptr;
  QGridLayout* open_layout_ = nullptr;
  QWidget* cards_host_ = nullptr;
  QGridLayout* cards_layout_ = nullptr;
  QWidget* empty_panel_ = nullptr;
  QLabel* empty_label_ = nullptr;
};

}  // namespace tamias
