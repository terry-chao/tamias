#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace tamias {

inline constexpr int kMaxRecentFiles = 8;

struct RecentFileItem {
  QString path;
  QString name;
  QDateTime opened_at;
  QDateTime created_at;
  QString thumbnail_path;
};

class RecentFilesStore {
 public:
  void load();
  void save() const;

  [[nodiscard]] const QVector<RecentFileItem>& items() const { return items_; }

  void add(const QString& path, const QString& thumbnail_path = QString());
  void remove(const QString& path);

 private:
  QVector<RecentFileItem> items_;
};

}  // namespace tamias
