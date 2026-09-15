#pragma once

#include "engine/math/math.h"

namespace tamias {

// 材质上的贴图变换。albedo / 法线共用一份（CAD 里两张图应对齐）。
// UV：uv * scale + offset。triplanar：world_pos * world_scale。
// world_scale 默认 2，与原先 shader 里写死的 `world_pos * 2` 一致。
struct TextureTransform {
  Vec2 scale{1.f, 1.f};
  Vec2 offset{0.f, 0.f};
  float rotation = 0.f;      // 弧度；一期 shader 尚未采样，先入库
  float world_scale = 2.f;
};

}  // namespace tamias
