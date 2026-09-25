#include "app/base/script_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace tamias {

QString scripts_directory() {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (base.isEmpty()) {
    return {};
  }
  return QDir(base).filePath(QStringLiteral("scripts"));
}

QStringList list_scripts() {
  const QString dir = scripts_directory();
  if (dir.isEmpty()) {
    return {};
  }
  QDir folder(dir);
  if (!folder.exists()) {
    return {};
  }
  QStringList names = folder.entryList({QStringLiteral("*.cs")}, QDir::Files, QDir::Name);
  QStringList paths;
  paths.reserve(names.size());
  for (const QString& name : names) {
    paths.push_back(folder.filePath(name));
  }
  return paths;
}

QString next_script_path() {
  const QString dir = scripts_directory();
  if (dir.isEmpty()) {
    return {};
  }
  for (int i = 1; i < 1000; ++i) {
    const QString candidate =
        QDir(dir).filePath(QStringLiteral("script%1.cs").arg(i));
    if (!QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return QDir(dir).filePath(QStringLiteral("script.cs"));
}

bool read_script(const QString& path, QString& text, QString& error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    error = file.errorString();
    return false;
  }
  QTextStream stream(&file);
  text = stream.readAll();
  return true;
}

bool write_script(const QString& path, const QString& text, QString& error) {
  const QFileInfo info(path);
  if (!info.absoluteDir().exists() &&
      !QDir().mkpath(info.absolutePath())) {
    error = QStringLiteral("cannot create %1").arg(info.absolutePath());
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    error = file.errorString();
    return false;
  }
  QTextStream stream(&file);
  // 脚本是文本资产：统一 LF，跨平台 diff 才干净。
  stream << QString(text).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  return true;
}

}  // namespace tamias
