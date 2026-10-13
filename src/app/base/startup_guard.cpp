#include "app/base/startup_guard.h"

#include "app/base/qt_path.h"

#include <QDir>
#include <QStandardPaths>

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace tamias {
namespace {

constexpr const char* kBackendPrefix = "backend=";

// 标记是给人看的小文本，不引 JSON 解析器：找 "backend=" 那一行，其余行忽略。
std::optional<GraphicsBackend> parse_backend(std::string_view text) {
  std::size_t pos = text.find(kBackendPrefix);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  pos += std::char_traits<char>::length(kBackendPrefix);
  std::size_t end = text.find_first_of("\r\n", pos);
  if (end == std::string_view::npos) {
    end = text.size();
  }
  return try_backend_from_name(text.substr(pos, end - pos));
}

}  // namespace

StartupGuard::StartupGuard() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (dir.isEmpty()) {
    return;  // 没有可写目录：安全模式这套就不生效，但不该影响启动
  }
  marker_path_ = qstring_to_path(dir) / "startup.marker";
  std::error_code ec;
  previous_incomplete_ = std::filesystem::exists(marker_path_, ec) && !ec;
  if (previous_incomplete_) {
    std::ifstream in(marker_path_, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    previous_backend_ = parse_backend(text);
  }
}

void StartupGuard::begin(GraphicsBackend backend) {
  if (marker_path_.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(marker_path_.parent_path(), ec);
  std::ofstream out(marker_path_, std::ios::trunc);
  out << "startup in progress\n";
  out << kBackendPrefix << to_string(backend) << "\n";
}

void StartupGuard::mark_started_ok() {
  if (marker_path_.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove(marker_path_, ec);
  previous_incomplete_ = false;
  previous_backend_.reset();
}

}  // namespace tamias
