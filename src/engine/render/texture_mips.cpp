#include "engine/render/texture_mips.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tamias {
namespace {

float srgb_u8_to_linear(std::uint8_t u) {
  const float x = static_cast<float>(u) / 255.f;
  return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
}

std::uint8_t linear_to_srgb_u8(float linear) {
  const float x = std::clamp(linear, 0.f, 1.f);
  const float srgb = x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.f / 2.4f) - 0.055f;
  return static_cast<std::uint8_t>(std::clamp(srgb, 0.f, 1.f) * 255.f + 0.5f);
}

void downsample(const std::uint8_t* src, std::uint32_t src_w, std::uint32_t src_h, bool srgb,
                std::uint8_t* dst, std::uint32_t dst_w, std::uint32_t dst_h) {
  for (std::uint32_t y = 0; y < dst_h; ++y) {
    for (std::uint32_t x = 0; x < dst_w; ++x) {
      float acc[4] = {0.f, 0.f, 0.f, 0.f};
      std::uint32_t n = 0;
      const std::uint32_t sx = x * 2;
      const std::uint32_t sy = y * 2;
      for (std::uint32_t oy = 0; oy < 2; ++oy) {
        const std::uint32_t iy = sy + oy;
        if (iy >= src_h) {
          continue;
        }
        for (std::uint32_t ox = 0; ox < 2; ++ox) {
          const std::uint32_t ix = sx + ox;
          if (ix >= src_w) {
            continue;
          }
          const std::uint8_t* p = src + (static_cast<std::size_t>(iy) * src_w + ix) * 4;
          if (srgb) {
            acc[0] += srgb_u8_to_linear(p[0]);
            acc[1] += srgb_u8_to_linear(p[1]);
            acc[2] += srgb_u8_to_linear(p[2]);
          } else {
            acc[0] += static_cast<float>(p[0]) / 255.f;
            acc[1] += static_cast<float>(p[1]) / 255.f;
            acc[2] += static_cast<float>(p[2]) / 255.f;
          }
          acc[3] += static_cast<float>(p[3]) / 255.f;
          ++n;
        }
      }
      const float inv = n == 0 ? 0.f : 1.f / static_cast<float>(n);
      std::uint8_t* o = dst + (static_cast<std::size_t>(y) * dst_w + x) * 4;
      if (srgb) {
        o[0] = linear_to_srgb_u8(acc[0] * inv);
        o[1] = linear_to_srgb_u8(acc[1] * inv);
        o[2] = linear_to_srgb_u8(acc[2] * inv);
      } else {
        o[0] = static_cast<std::uint8_t>(std::clamp(acc[0] * inv, 0.f, 1.f) * 255.f + 0.5f);
        o[1] = static_cast<std::uint8_t>(std::clamp(acc[1] * inv, 0.f, 1.f) * 255.f + 0.5f);
        o[2] = static_cast<std::uint8_t>(std::clamp(acc[2] * inv, 0.f, 1.f) * 255.f + 0.5f);
      }
      o[3] = static_cast<std::uint8_t>(std::clamp(acc[3] * inv, 0.f, 1.f) * 255.f + 0.5f);
    }
  }
}

}  // namespace

std::uint32_t texture_mip_levels(std::uint32_t width, std::uint32_t height) {
  if (width == 0 || height == 0) {
    return 1;
  }
  std::uint32_t n = 1;
  std::uint32_t w = width;
  std::uint32_t h = height;
  while (w > 1 || h > 1) {
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
    ++n;
  }
  return n;
}

std::vector<std::vector<std::uint8_t>> build_texture_mips(const TextureAsset& tex) {
  std::vector<std::vector<std::uint8_t>> mips;
  const std::uint32_t levels = texture_mip_levels(tex.width, tex.height);
  mips.resize(levels);
  mips[0] = tex.rgba;
  std::uint32_t w = tex.width;
  std::uint32_t h = tex.height;
  for (std::uint32_t i = 1; i < levels; ++i) {
    const std::uint32_t nw = std::max(1u, w / 2);
    const std::uint32_t nh = std::max(1u, h / 2);
    mips[i].assign(static_cast<std::size_t>(nw) * nh * 4, 0);
    downsample(mips[i - 1].data(), w, h, tex.srgb, mips[i].data(), nw, nh);
    w = nw;
    h = nh;
  }
  return mips;
}

}  // namespace tamias
