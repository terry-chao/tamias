#pragma once

#include "recent_files.h"

#include <QWidget>

class QGridLayout;
class QLabel;

namespace tamias {

class HomePage final : public QWidget {
  Q_OBJECT
 public:
  explicit HomePage(QWidget* parent = nullptr);

  void refresh(const QVector<RecentFileItem>& items);

 signals:
  void newDemoRequested();
  void openRequested();
  void fileActivated(const QString& path);
  void missingFileActivated(const QString& path);

 private:
  void clear_cards();

  QWidget* cards_host_ = nullptr;
  QGridLayout* cards_layout_ = nullptr;
  QWidget* empty_panel_ = nullptr;
  QLabel* empty_label_ = nullptr;
};

}  // namespace tamias
