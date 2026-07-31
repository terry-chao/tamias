#include "recent_files.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantList>
#include <QVariantMap>

namespace tamias {
namespace {

QDateTime file_created_at(const QFileInfo& info) {
  const QDateTime birth = info.birthTime();
  if (birth.isValid()) {
    return birth;
  }
  const QDateTime meta = info.metadataChangeTime();
  if (meta.isValid()) {
    return meta;
  }
  return info.lastModified();
}

QString normalize_path(const QString& path) {
  return QFileInfo(path).absoluteFilePath();
}

bool is_cached_thumbnail(const QString& path) {
  if (path.isEmpty()) {
    return false;
  }
  const QString root =
      QFileInfo(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                    .filePath(QStringLiteral("thumbnails")))
          .absoluteFilePath();
  return QFileInfo(path).absoluteFilePath().startsWith(root);
}

void remove_thumbnail_file(const QString& path) {
  if (is_cached_thumbnail(path)) {
    QFile::remove(path);
  }
}

}  // namespace

void RecentFilesStore::load() {
  items_.clear();
  QSettings settings;
  const QVariantList list = settings.value(QStringLiteral("recent/items")).toList();
  for (const QVariant& entry : list) {
    const QVariantMap map = entry.toMap();
    RecentFileItem item;
    item.path = map.value(QStringLiteral("path")).toString();
    item.name = map.value(QStringLiteral("name")).toString();
    item.opened_at = QDateTime::fromString(map.value(QStringLiteral("openedAt")).toString(), Qt::ISODate);
    item.created_at =
        QDateTime::fromString(map.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    item.thumbnail_path = map.value(QStringLiteral("thumbnailPath")).toString();
    if (item.path.isEmpty()) {
      continue;
    }
    if (item.name.isEmpty()) {
      item.name = QFileInfo(item.path).fileName();
    }
    items_.push_back(std::move(item));
    if (items_.size() >= kMaxRecentFiles) {
      break;
    }
  }
}

void RecentFilesStore::save() const {
  QVariantList list;
  list.reserve(items_.size());
  for (const RecentFileItem& item : items_) {
    QVariantMap map;
    map.insert(QStringLiteral("path"), item.path);
    map.insert(QStringLiteral("name"), item.name);
    map.insert(QStringLiteral("openedAt"), item.opened_at.toString(Qt::ISODate));
    map.insert(QStringLiteral("createdAt"), item.created_at.toString(Qt::ISODate));
    map.insert(QStringLiteral("thumbnailPath"), item.thumbnail_path);
    list.push_back(map);
  }
  QSettings settings;
  settings.setValue(QStringLiteral("recent/items"), list);
}

void RecentFilesStore::add(const QString& path, const QString& thumbnail_path) {
  const QString absolute = normalize_path(path);
  if (absolute.isEmpty()) {
    return;
  }

  QString previous_thumb;
  for (int i = 0; i < items_.size(); ++i) {
    if (QFileInfo(items_[i].path).absoluteFilePath() == absolute) {
      previous_thumb = items_[i].thumbnail_path;
      items_.removeAt(i);
      break;
    }
  }

  const QFileInfo info(absolute);
  RecentFileItem item;
  item.path = absolute;
  item.name = info.fileName();
  item.opened_at = QDateTime::currentDateTime();
  item.created_at = file_created_at(info);
  item.thumbnail_path = thumbnail_path;
  if (item.thumbnail_path.isEmpty()) {
    item.thumbnail_path = previous_thumb;
  } else if (!previous_thumb.isEmpty() && previous_thumb != item.thumbnail_path) {
    remove_thumbnail_file(previous_thumb);
  }
  items_.prepend(std::move(item));

  while (items_.size() > kMaxRecentFiles) {
    remove_thumbnail_file(items_.last().thumbnail_path);
    items_.removeLast();
  }
  save();
}

void RecentFilesStore::remove(const QString& path) {
  const QString absolute = normalize_path(path);
  for (int i = 0; i < items_.size(); ++i) {
    if (QFileInfo(items_[i].path).absoluteFilePath() == absolute) {
      remove_thumbnail_file(items_[i].thumbnail_path);
      items_.removeAt(i);
      save();
      return;
    }
  }
}

}  // namespace tamias
