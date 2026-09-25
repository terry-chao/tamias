#pragma once

#include "engine/document/document.h"

#include <cmath>
#include <cstdint>

namespace tamias {

// 「武装中的构件命令是照哪一层摆的」——楼层 id + 该层标高 + 该层层高。
//
// 交互式命令的标高 / 工作面是**武装那一刻**按当时楼层算的，而楼层归属是
// **执行那一刻**现取的（`Document::assign_active_storey`）。用户点齐两点之前
// 只要切了楼层（楼层管理双击开另一层视图、楼层面板换当前楼层、改标高 / 层高），
// 两者就对不上：构件落在旧楼层的标高上、却挂到新楼层——"在当前楼层画的板
// 归属到了上一个楼层"。所以视口必须拿武装时的这份快照跟当前楼层比，
// 变了就按新楼层重新武装（见 `DocumentViewport::sync_armed_placement`）。
struct ArmedPlacement {
  std::uint64_t storey_id = 0;
  double elevation = 0.0;
  double height = 0.0;
  bool valid = false;  // 没有武装中的构件命令时为 false
};

inline ArmedPlacement capture_armed_placement(const Document& doc) {
  const std::uint64_t storey_id = doc.bim().active_storey_id();
  const Storey* storey = doc.bim().find_storey(storey_id);
  ArmedPlacement placement;
  placement.storey_id = storey_id;
  placement.elevation = doc.bim().storey_elevation(storey_id);
  placement.height = storey != nullptr ? storey->height : 0.0;
  placement.valid = true;
  return placement;
}

// 武装时的楼层和现在的楼层还是不是同一个"放置状态"？层高也算：改层高会动板默认
// 画的"本层顶"，标高变了会把构件挪走——三种都得重画一遍。
inline bool armed_placement_stale(const ArmedPlacement& armed, const Document& doc) {
  if (!armed.valid) {
    return false;
  }
  const std::uint64_t storey_id = doc.bim().active_storey_id();
  if (storey_id != armed.storey_id) {
    return true;
  }
  const Storey* storey = doc.bim().find_storey(storey_id);
  const double height = storey != nullptr ? storey->height : 0.0;
  return std::abs(doc.bim().storey_elevation(storey_id) - armed.elevation) > 1e-9 ||
         std::abs(height - armed.height) > 1e-9;
}

}  // namespace tamias
