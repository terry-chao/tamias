#include "app/base/startup_guard.h"

#include "app/base/qt_path.h"

#include <QDir>
#include <QStandardPaths>

#include <fstream>
#include <system_error>

namespace tamias {

StartupGuard::StartupGuard() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (dir.isEmpty()) {
    return;  // 没有可写目录：安全模式这套就不生效，但不该影响启动
  }
  marker_path_ = qstring_to_path(dir) / "startup.marker";
  std::error_code ec;
  previous_incomplete_ = std::filesystem::exists(marker_path_, ec) && !ec;
}

void StartupGuard::begin() {
  if (marker_path_.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(marker_path_.parent_path(), ec);
  std::ofstream out(marker_path_, std::ios::trunc);
  out << "startup in progress\n";
}

void StartupGuard::mark_started_ok() {
  if (marker_path_.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove(marker_path_, ec);
  previous_incomplete_ = false;
}

}  // namespace tamias
