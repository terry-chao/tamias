#pragma once

namespace tamias {

// 默认墙高，也是默认楼层净高；楼板默认落在该标高。
inline constexpr double kDefaultWallHeight = 3.0;
inline constexpr double kDefaultWallThickness = 0.2;

struct WallSize {
  double thickness = kDefaultWallThickness;
  double length = 1.0;
  double height = kDefaultWallHeight;
};

}  // namespace tamias
