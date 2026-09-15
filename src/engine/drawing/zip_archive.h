#pragma once

#include "engine/base/result.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tamias {

// 最小 ZIP 读取器：只读，只管把 DWF / DWFx 这类包里的条目取出来。
// 支持 store（0）与 deflate（8）两种压缩方式；加密、zip64、分卷一律明确报错。
// Qt-free（引擎侧），所以能无头测。
class ZipArchive {
 public:
  // 只持有 bytes 的视图，不拷贝：整包必须比本对象活得久。
  static Result<ZipArchive> open(std::span<const std::uint8_t> bytes);
  // 只认本地文件头，用来快速判断"这是不是个 ZIP 包"。
  [[nodiscard]] static bool looks_like_zip(std::span<const std::uint8_t> bytes);

  [[nodiscard]] const std::vector<std::string>& names() const { return names_; }
  [[nodiscard]] bool contains(std::string_view name) const;
  // 解出某个条目。名字要写全（区分大小写，和 ZIP 目录里一致）。
  [[nodiscard]] Result<std::vector<std::uint8_t>> extract(std::string_view name) const;

 private:
  struct Entry {
    std::uint64_t local_header = 0;
    std::uint64_t compressed_size = 0;
    std::uint64_t size = 0;
    std::uint16_t method = 0;
  };

  std::span<const std::uint8_t> bytes_;
  std::vector<std::string> names_;
  std::unordered_map<std::string, Entry> entries_;
};

}  // namespace tamias
