#pragma once

#include "engine/math/math.h"
#include "engine/render/scene_debug_player.h"
#include "engine/render/scene_debug_skip_reason.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tamias {

// 货单条目命运 + RecordCommands 实际发出的 draw_indexed。
struct SceneDebugLog {
  struct Item {
    std::size_t index = 0;
    std::uint64_t node_id = 0;
    std::uint64_t mesh_asset_id = 0;
    SceneDebugSkipReason reason = SceneDebugSkipReason::Drawn;
    std::uint32_t triangles = 0;
  };

  struct Draw {
    std::size_t index = 0;
    std::uint32_t index_count = 0;
    std::uint32_t instance_count = 0;
    bool transparent = false;
    bool lines = false;
    std::string pipeline;
  };

  std::vector<Item> items;
  std::vector<Draw> draws;
  std::uint32_t drawn_items = 0;
  std::uint32_t hidden = 0;
  std::uint32_t isolated = 0;
  std::uint32_t stepped = 0;
  std::uint32_t culled = 0;
};

[[nodiscard]] const char* scene_debug_skip_reason_name(SceneDebugSkipReason reason);

// 用当前 Player 过滤结果重跑场景图录制（无 GPU）。frustum 为空则不视锥剔除。
[[nodiscard]] SceneDebugLog capture_scene_debug_log(const SceneDebugPlayer& player,
                                                    const Frustum* frustum = nullptr);

}  // namespace tamias
