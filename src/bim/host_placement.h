#pragma once

namespace tamias {

// 开口（门/窗）在宿主墙上的参数化位置。墙一动，用这组参数重算放置。
struct HostPlacement {
  double along = 0.5;  // 沿墙长：0 = 起点，1 = 终点
  double sill = 0.9;   // 距墙底的离地高度（米）；门默认 0，窗默认 0.9
  double offset = 0.0; // 沿墙厚：0 = 墙中心
  double handle_side = 1.0; // 门把手侧：+1 = 门扇局部 +X 侧，-1 = 门扇局部 -X 侧
};

}  // namespace tamias
