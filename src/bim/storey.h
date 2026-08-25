#pragma once

#include <cstdint>
#include <string>

namespace tamias {

struct Storey {
  std::uint64_t id = 0;  // 与无网格 SceneNode 共用 id。
  std::string name;
  double elevation = 0.0;
};

}  // namespace tamias
