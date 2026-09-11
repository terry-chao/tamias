#pragma once

#include "engine/render/texture_asset.h"

#include <cstdint>
#include <vector>

namespace tamias {

[[nodiscard]] std::uint32_t texture_mip_levels(std::uint32_t width, std::uint32_t height);

// mip 0 = 原图像素拷贝；后续为 2×2 box filter。sRGB 在线性空间平均。
[[nodiscard]] std::vector<std::vector<std::uint8_t>> build_texture_mips(const TextureAsset& tex);

}  // namespace tamias
