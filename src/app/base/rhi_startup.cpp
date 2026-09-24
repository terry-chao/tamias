#include "app/base/rhi_startup.h"

#include "app/base/qt_path.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QString>

#include <filesystem>
#include <string>
#include <vector>

namespace tamias {
namespace {

std::optional<std::string> read_text_file(const std::filesystem::path& path) {
  QFile file(path_to_qstring(path));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return std::nullopt;
  }
  const QByteArray bytes = file.readAll();
  return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// --key=value / --key / --flag
std::optional<QString> argument_value(const QStringList& arguments, const QString& key) {
  for (int i = 1; i < arguments.size(); ++i) {
    const QString& argument = arguments[i];
    if (argument.compare(key, Qt::CaseInsensitive) == 0) {
      return QString();  // 裸开关
    }
    if (argument.startsWith(key + QLatin1Char('='), Qt::CaseInsensitive)) {
      return argument.mid(key.size() + 1);
    }
  }
  return std::nullopt;
}

}  // namespace

RhiCliOptions parse_rhi_cli(const QStringList& arguments) {
  RhiCliOptions options;
  options.probe = argument_value(arguments, QStringLiteral("--probe-rhi")).has_value();
  options.json = argument_value(arguments, QStringLiteral("--json")).has_value();
  options.safe_mode = argument_value(arguments, QStringLiteral("--safe-mode")).has_value();
  if (const auto backend = argument_value(arguments, QStringLiteral("--gpu-backend"));
      backend.has_value() && !backend->isEmpty()) {
    options.backend = try_backend_from_name(backend->toStdString());
  }
  if (const auto view = argument_value(arguments, QStringLiteral("--render-view"));
      view.has_value() && !view->isEmpty()) {
    options.render_view_path = *view;
  }
  if (const auto size = argument_value(arguments, QStringLiteral("--render-size"));
      size.has_value() && !size->isEmpty()) {
    const QStringList parts = size->split(QLatin1Char('x'), Qt::SkipEmptyParts);
    if (parts.size() == 2) {
      bool ok_width = false;
      bool ok_height = false;
      const uint width = parts[0].toUInt(&ok_width);
      const uint height = parts[1].toUInt(&ok_height);
      if (ok_width && ok_height && width > 0 && height > 0) {
        options.render_width = width;
        options.render_height = height;
      }
    }
  }
  return options;
}

RhiBlocklist load_rhi_blocklist(QStringList* warnings) {
  std::vector<std::string> engine_warnings;
  for (const std::filesystem::path& dir : app_asset_dirs()) {
    const std::filesystem::path path = dir / "rhi_blocklist.json";
    const std::optional<std::string> text = read_text_file(path);
    if (!text.has_value()) {
      continue;  // 没有这份文件就是空名单
    }
    RhiBlocklist list = parse_rhi_blocklist(*text, &engine_warnings);
    if (warnings != nullptr) {
      for (const std::string& warning : engine_warnings) {
        warnings->push_back(QString::fromStdString(warning));
      }
    }
    return list;
  }
  return {};
}

RhiPolicy load_rhi_policy(QStringList* warnings) {
  std::vector<std::filesystem::path> candidates;
  const QString program_data = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  if (!program_data.isEmpty()) {
    candidates.push_back(qstring_to_path(program_data) / "tamias" / "rhi_policy.json");
  }
  candidates.push_back(qstring_to_path(QCoreApplication::applicationDirPath()) / "rhi_policy.json");

  std::vector<std::string> engine_warnings;
  for (const std::filesystem::path& path : candidates) {
    const std::optional<std::string> text = read_text_file(path);
    if (!text.has_value()) {
      continue;
    }
    RhiPolicy policy = parse_rhi_policy(*text, &engine_warnings);
    if (warnings != nullptr) {
      for (const std::string& warning : engine_warnings) {
        warnings->push_back(QString::fromStdString(warning));
      }
    }
    return policy;
  }
  return {};
}

}  // namespace tamias
