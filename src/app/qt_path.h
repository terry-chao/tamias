#pragma once

#include "engine/core/fs_utf8.h"

#include <QString>

#include <filesystem>
#include <string>

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

}  // namespace tamias
