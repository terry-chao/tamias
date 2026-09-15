#pragma once

#include <cstdint>

namespace tamias {

// 纹理在材质槽里的预期用途。同一份像素按 sRGB/线性区分，不按 usage 去重。
enum class TextureUsage : std::uint8_t {
  Unknown = 0,
  Albedo = 1,
  Normal = 2,
  Orm = 3,  // packed occlusion (R) / roughness (G) / metallic (B)
};

}  // namespace tamias
