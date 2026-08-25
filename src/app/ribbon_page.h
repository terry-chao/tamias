#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

class QHBoxLayout;

namespace tamias {

class RibbonGroup;

class RibbonPage final : public QWidget {
  Q_OBJECT
 public:
  explicit RibbonPage(QWidget* parent = nullptr);

  RibbonGroup* add_group(const QString& title);
  RibbonGroup* add_group(const QString& id, const QString& title);
  [[nodiscard]] RibbonGroup* find_group(const QString& id) const;

 private:
  QHBoxLayout* groups_layout_ = nullptr;
  RibbonGroup* last_group_ = nullptr;
  QHash<QString, RibbonGroup*> groups_by_id_;
};

}  // namespace tamias
