#pragma once

#include "engine/base/fs_utf8.h"

#include <QCoreApplication>
#include <QString>

#include <filesystem>
#include <string>
#include <vector>

namespace tamias {

inline std::filesystem::path qstring_to_path(const QString& path) {
  const QByteArray u8 = path.toUtf8();
  return std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t*>(u8.constData()), static_cast<std::size_t>(u8.size())));
}

inline QString path_to_qstring(const std::filesystem::path& path) {
  const std::string u8 = path_to_utf8(path);
  return QString::fromUtf8(u8.data(), static_cast<int>(u8.size()));
}

// 资源目录：仓库里的 assets/（开发时）→ exe 旁边的 assets/（部署时）。只返回存在的。
[[nodiscard]] inline std::vector<std::filesystem::path> app_asset_dirs() {
  std::vector<std::filesystem::path> dirs;
  const auto add = [&dirs](const std::filesystem::path& dir) {
    std::error_code ec;
    if (!dir.empty() && std::filesystem::is_directory(dir, ec)) {
      dirs.push_back(dir);
    }
  };
#if defined(TAMIAS_SOURCE_DIR)
  add(std::filesystem::path(TAMIAS_SOURCE_DIR) / "assets");
#endif
  add(qstring_to_path(QCoreApplication::applicationDirPath()) / "assets");
  return dirs;
}

}  // namespace tamias
