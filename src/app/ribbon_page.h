#pragma once

#include <QWidget>

class QHBoxLayout;

namespace tamias {

class RibbonGroup;

class RibbonPage final : public QWidget {
  Q_OBJECT
 public:
  explicit RibbonPage(QWidget* parent = nullptr);

  RibbonGroup* add_group(const QString& title);

 private:
  QHBoxLayout* groups_layout_ = nullptr;
  RibbonGroup* last_group_ = nullptr;
};

}  // namespace tamias
