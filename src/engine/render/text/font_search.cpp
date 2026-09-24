#include "engine/render/text/font_search.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <system_error>

namespace tamias {
namespace {

// 默认正文字体的偏好顺序（大小写无关的子串匹配）。挑中第一个命中的文件。
constexpr const char* kPreferredFamilies[] = {
    "segoeui",     // Windows
    "dejavusans",  // Linux
    "notosans",    // 跨平台
    "arial",
    "helvetica",
    "roboto",
    "opensans",
    "liberationsans",
};

// 中文字体的偏好顺序（大小写无关的子串匹配）。
constexpr const char* kPreferredCjkFamilies[] = {
    "msyh",      // 微软雅黑
    "dengxian",  // 等线
    "simhei",    // 黑体
    "simsun",    // 宋体
    "notosanscjk",
    "notosanssc",
    "sourcehansans",
    "sourcehanserif",
};

bool is_font_extension(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".ttf" || ext == ".otf" || ext == ".ttc";
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

}  // namespace

std::vector<std::filesystem::path> default_font_dirs() {
  std::vector<std::filesystem::path> dirs;
  const auto add = [&dirs](const std::filesystem::path& path) {
    if (path.empty()) {
      return;
    }
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
      dirs.push_back(path);
    }
  };
#if defined(_WIN32)
  add("C:/Windows/Fonts");
  if (const char* local = std::getenv("LOCALAPPDATA")) {
    add(std::filesystem::path(local) / "Microsoft/Windows/Fonts");
  }
#elif defined(__APPLE__)
  add("/System/Library/Fonts");
  add("/Library/Fonts");
  if (const char* home = std::getenv("HOME")) {
    add(std::filesystem::path(home) / "Library/Fonts");
  }
#else
  add("/usr/share/fonts");
  add("/usr/local/share/fonts");
  if (const char* home = std::getenv("HOME")) {
    add(std::filesystem::path(home) / ".fonts");
    add(std::filesystem::path(home) / ".local/share/fonts");
  }
#endif
  return dirs;
}

std::vector<std::filesystem::path> list_font_files(
    const std::vector<std::filesystem::path>& dirs) {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  for (const std::filesystem::path& dir : dirs) {
    if (!std::filesystem::is_directory(dir, ec)) {
      continue;
    }
    // 系统字体目录常有一层子目录（Linux 尤其），递归但限制深度。
    for (std::filesystem::recursive_directory_iterator it(
             dir, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
      const std::filesystem::path& candidate = it->path();
      if (!it->is_regular_file(ec)) {
        continue;
      }
      if (is_font_extension(candidate)) {
        files.push_back(candidate);
      }
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::optional<FontCandidate> pick_default_font(const std::vector<std::filesystem::path>& dirs) {
  const std::vector<std::filesystem::path> files = list_font_files(dirs);
  if (files.empty()) {
    return std::nullopt;
  }
  for (const char* preferred : kPreferredFamilies) {
    for (const std::filesystem::path& file : files) {
      if (lowercase(file.filename().string()).find(preferred) != std::string::npos) {
        return FontCandidate{file, 0};
      }
    }
  }
  return FontCandidate{files.front(), 0};
}

std::optional<FontCandidate> pick_cjk_font(const std::vector<std::filesystem::path>& dirs) {
  const std::vector<std::filesystem::path> files = list_font_files(dirs);
  for (const char* preferred : kPreferredCjkFamilies) {
    for (const std::filesystem::path& file : files) {
      if (lowercase(file.filename().string()).find(preferred) != std::string::npos) {
        return FontCandidate{file, 0};
      }
    }
  }
  return std::nullopt;
}

}  // namespace tamias
