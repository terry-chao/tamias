#pragma once

#include <filesystem>

namespace tamias {

// 一个候选字体文件。.ttc / .otc 是字体集合，要指明用第几套字。
struct FontCandidate {
  std::filesystem::path path;
  int face_index = 0;
};

}  // namespace tamias
