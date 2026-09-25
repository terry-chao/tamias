#pragma once

#include <QObject>
#include <filesystem>
#include <vector>

class QFileSystemWatcher;
class QTimer;

namespace tamias {

// 盯着扩展的约定目录：**只负责说「有动静」**。
// 谁真的变了由托管侧的内容指纹说了算（编辑器写临时文件、我们自己的写入都不该触发重载）。
//
// QFileSystemWatcher 不递归，所以每个根 + 每个一级子目录都单独挂上。
// 文件系统事件又碎又密（保存一次可能连着好几个），攒 250ms 再报一次。
class ExtensionWatcher final : public QObject {
  Q_OBJECT
 public:
  explicit ExtensionWatcher(std::vector<std::filesystem::path> roots, QObject* parent = nullptr);

  // 重载之后调一次：目录可能新增或消失。
  void rewatch();

 signals:
  void changed();

 private:
  std::vector<std::filesystem::path> roots_;
  QFileSystemWatcher* watcher_ = nullptr;
  QTimer* debounce_ = nullptr;
};

}  // namespace tamias
