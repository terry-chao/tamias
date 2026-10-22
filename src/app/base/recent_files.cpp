#include "app/base/recent_files.h"

#include "app/base/app_settings.h"
#include "engine/base/log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantList>
#include <QVariantMap>

namespace tamias {
namespace {

constexpr int kRecentFormatVersion = 1;

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

// 最近文件是「这台机器上的使用痕迹」，不是偏好：放在 <AppData>/tamias/tamias/ 下，
// 和缩略图、脚本做邻居。以前它在 tamias.ini 的 recent/items 里，是 QVariant 的
// @Variant(\0\0\0\b...) 转义块——5 条记录就 8 KB，占整份 ini 的八成，还只有
// QSettings 读得懂。现在是一份可读、可手清、可测的 JSON。
QString recent_file_path() {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (base.isEmpty()) {
    return {};
  }
  return QDir(base).filePath(QStringLiteral("recent.json"));
}

QString format_time(const QDateTime& time) {
  return time.isValid() ? time.toString(Qt::ISODate) : QString();
}

QDateTime parse_time(const QString& text) {
  return text.isEmpty() ? QDateTime() : QDateTime::fromString(text, Qt::ISODate);
}

// 老 QVariantMap 和新 JSON 对象用同一套字段名，所以解析只有这一份。
RecentFileItem item_from_fields(const QVariantMap& fields) {
  RecentFileItem item;
  item.path = fields.value(QStringLiteral("path")).toString();
  item.name = fields.value(QStringLiteral("name")).toString();
  item.opened_at = parse_time(fields.value(QStringLiteral("openedAt")).toString());
  item.created_at = parse_time(fields.value(QStringLiteral("createdAt")).toString());
  item.thumbnail_path = fields.value(QStringLiteral("thumbnailPath")).toString();
  return item;
}

void append_loaded(QVector<RecentFileItem>* items, RecentFileItem item) {
  if (item.path.isEmpty() || items->size() >= kMaxRecentFiles) {
    return;
  }
  if (item.name.isEmpty()) {
    item.name = QFileInfo(item.path).fileName();
  }
  items->push_back(std::move(item));
}

}  // namespace

void RecentFilesStore::load() {
  items_.clear();
  const QString path = recent_file_path();
  if (path.isEmpty()) {
    return;
  }

  QFile file(path);
  if (!file.exists()) {
    // 第一次跑这个版本：把 ini 里的老 recent/items 搬过来，写成 JSON，再把老键删掉。
    // 只搬一次——recent.json 一存在，这里就不再走。
    QSettings settings = open_settings();
    const QVariantList legacy = settings.value(QStringLiteral("recent/items")).toList();
    if (legacy.isEmpty()) {
      return;
    }
    for (const QVariant& entry : legacy) {
      append_loaded(&items_, item_from_fields(entry.toMap()));
    }
    settings.remove(QStringLiteral("recent/items"));
    settings.sync();
    save();
    log_info("Recent files moved out of the settings file: " + path.toStdString());
    return;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    log_warn("Cannot read recent files: " + path.toStdString());
    return;
  }
  QJsonParseError parse_error{};
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    // 文件坏了就当没有（下次 add 会整份重写）；不自作主张删，留给用户自己看。
    log_warn("Cannot parse recent files: " + path.toStdString());
    return;
  }
  const QJsonArray entries = document.object().value(QStringLiteral("items")).toArray();
  for (const QJsonValue& entry : entries) {
    append_loaded(&items_, item_from_fields(entry.toObject().toVariantMap()));
  }
}

void RecentFilesStore::save() const {
  const QString path = recent_file_path();
  if (path.isEmpty()) {
    return;
  }

  QJsonArray entries;
  for (const RecentFileItem& item : items_) {
    QJsonObject entry;
    entry.insert(QStringLiteral("path"), item.path);
    entry.insert(QStringLiteral("name"), item.name);
    entry.insert(QStringLiteral("openedAt"), format_time(item.opened_at));
    entry.insert(QStringLiteral("createdAt"), format_time(item.created_at));
    if (!item.thumbnail_path.isEmpty()) {
      entry.insert(QStringLiteral("thumbnailPath"), item.thumbnail_path);
    }
    entries.append(entry);
  }
  QJsonObject root;
  root.insert(QStringLiteral("version"), kRecentFormatVersion);
  root.insert(QStringLiteral("items"), entries);

  // 原子写：QSaveFile 先写同目录的临时文件再改名，写一半断电也不会留下半份 JSON。
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    log_warn("Cannot write recent files: " + path.toStdString());
    return;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    log_warn("Cannot commit recent files: " + path.toStdString());
  }
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
