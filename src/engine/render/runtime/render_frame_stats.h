#pragma once

#include <cstdint>

namespace tamias {

struct RenderFrameStats {
  std::uint32_t draws = 0;
  std::uint32_t triangles = 0;
  std::uint64_t gpu_mesh_bytes = 0;
  std::uint32_t pending_tessellate = 0;
  std::uint32_t lod_requests = 0;
  // overlay 通道的屏幕空间像素线：本帧收集了多少条线段、发了几次实例化 draw。
  // 这两条一起看才知道合批有没有生效（逐段提交那会儿两个数都是线段数）。
  std::uint32_t overlay_line_instances = 0;
  std::uint32_t overlay_line_draws = 0;
};

}  // namespace tamias
