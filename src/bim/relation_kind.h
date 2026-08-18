#pragma once

#include <cstdint>

namespace tamias {

// 构件之间的显式关联。不靠 SceneNode.parent，也不靠坐标反推。
enum class RelationKind : std::uint8_t {
  HostedOn = 0,  // from 寄宿在 to 上（窗/门 → 墙）
};

}  // namespace tamias
