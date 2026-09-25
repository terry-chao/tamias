#pragma once

#include <cctype>
#include <filesystem>
#include <string>

namespace tamias {

// Windows 上 path::string() 走 ANSI 代码页，含中文等字符时会抛 std::system_error。
// 错误信息和扩展名比较一律用 UTF-8。
inline std::string path_to_utf8(const std::filesystem::path& path) {
  const auto u8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// UTF-8 → path：Windows 上**不能**直接喂 std::string（那是 ANSI 代码页，中文路径会乱码
// 甚至解析失败），必须先还原成 char8_t 序列。
inline std::filesystem::path path_from_utf8(std::string_view text) {
  return std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t*>(text.data()), static_cast<std::size_t>(text.size())));
}

inline std::string path_extension_lower(const std::filesystem::path& path) {
  auto ext = path_to_utf8(path.extension());
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext;
}

}  // namespace tamias
