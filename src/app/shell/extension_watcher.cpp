#include "app/shell/extension_watcher.h"

#include "app/base/qt_path.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStringList>
#include <QTimer>

#include <utility>

namespace tamias {
namespace {

// 编辑器保存是一串写操作，等它安静下来再动手。
constexpr int kDebounceMs = 250;

}  // namespace

ExtensionWatcher::ExtensionWatcher(std::vector<std::filesystem::path> roots, QObject* parent)
    : QObject(parent), roots_(std::move(roots)) {
  watcher_ = new QFileSystemWatcher(this);
  debounce_ = new QTimer(this);
  debounce_->setSingleShot(true);
  debounce_->setInterval(kDebounceMs);
  connect(debounce_, &QTimer::timeout, this, &ExtensionWatcher::changed);
  const auto arm = [this](const QString&) { debounce_->start(); };
  connect(watcher_, &QFileSystemWatcher::directoryChanged, this, arm);
  connect(watcher_, &QFileSystemWatcher::fileChanged, this, arm);
  rewatch();
}

void ExtensionWatcher::set_roots(std::vector<std::filesystem::path> roots) {
  roots_ = std::move(roots);
  rewatch();
}

void ExtensionWatcher::rewatch() {
  const QStringList watched = watcher_->directories() + watcher_->files();
  if (!watched.isEmpty()) {
    watcher_->removePaths(watched);
  }
  for (const auto& root : roots_) {
    const QString path = path_to_qstring(root);
    if (!QFileInfo::exists(path)) {
      continue;  // 根目录还没建起来（比如用户目录是空的），下次 rewatch 再说
    }
    // LoadExtension 可以直接指一个入口文件（.cs / .dll）：那就盯这个文件本身，
    // 它所在目录不在扫描表里，没有别的东西会替它报"有动静"。
    if (QFileInfo(path).isFile()) {
      watcher_->addPath(path);
      continue;
    }
    watcher_->addPath(path);
    const QDir dir(path);
    for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      watcher_->addPath(entry.absoluteFilePath());
    }
  }
}

}  // namespace tamias
