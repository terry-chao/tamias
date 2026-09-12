#pragma once

#include "bim/wall_size.h"

#include <cstdint>
#include <string>

namespace tamias {

struct Storey {
  std::uint64_t id = 0;  // 与无网格 SceneNode 共用 id。
  std::string name;
  double elevation = 0.0;
  // 层高：本层标高到上一层标高的距离；顶层没有上一层时用它定本层顶。
  double height = kDefaultWallHeight;
  // 夹层也是楼层（标高落在相邻两层之间、层高通常更小），只多一个标记，
  // 让楼层列表 / 楼层设置能把它和整层分开显示。
  bool mezzanine = false;
};

}  // namespace tamias
