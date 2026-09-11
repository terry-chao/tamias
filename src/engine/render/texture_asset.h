#pragma once

#include "engine/render/texture_usage.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tamias {

// 已解码的 RGBA8 纹理。引擎侧只存字节，图片解码由 app 层用 QImage 完成，
// 再把字节交给 Document::import_texture —— 引擎保持 Qt-free。
struct TextureAsset {
  std::uint64_t id = 0;
  std::string name;
  std::string source_path;  // 可空；仅便于重新导入，不参与内容指纹
  std::uint64_t content_hash = 0;
  std::uint64_t generation = 1;  // 像素被 replace 时递增；GPU 缓存据此失效
  TextureUsage usage = TextureUsage::Unknown;
  std::string builtin_key;  // 非空 = 程序化内置贴图，.tdoc 可省略像素
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;  // RGBA8，size == width * height * 4
  bool srgb = true;                // false = 线性（法线 / ORM）
};

// 内容指纹：宽高 + 颜色空间 + 像素。不含 id / 名称 / 路径 / usage / generation。
inline std::uint64_t texture_content_hash(const TextureAsset& tex) {
  constexpr std::uint64_t kOffset = 14695981039346656037ull;
  constexpr std::uint64_t kPrime = 1099511628211ull;
  std::uint64_t h = kOffset;
  const auto mix_u8 = [&](std::uint8_t v) {
    h ^= v;
    h *= kPrime;
  };
  const auto mix_bytes = [&](const void* data, std::size_t n) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < n; ++i) {
      mix_u8(p[i]);
    }
  };
  mix_bytes(&tex.width, sizeof(tex.width));
  mix_bytes(&tex.height, sizeof(tex.height));
  mix_u8(tex.srgb ? 1u : 0u);
  const std::uint64_t nbytes = tex.rgba.size();
  mix_bytes(&nbytes, sizeof(nbytes));
  if (!tex.rgba.empty()) {
    mix_bytes(tex.rgba.data(), tex.rgba.size());
  }
  return h == 0 ? 1u : h;
}

}  // namespace tamias
