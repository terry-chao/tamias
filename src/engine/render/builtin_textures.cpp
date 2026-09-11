#include "engine/render/builtin_textures.h"

#include "engine/math/math.h"

#include <cmath>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace tamias {
namespace {

constexpr std::uint32_t kMaterialTextureSize = 512;
constexpr float kPi = 3.14159265358979f;
constexpr float kInvSize = 1.f / static_cast<float>(kMaterialTextureSize);

std::uint8_t linear_to_srgb_u8(float linear) {
  const float x = std::clamp(linear, 0.f, 1.f);
  const float srgb = x <= 0.0031308f ? 12.92f * x
                                     : 1.055f * std::pow(x, 1.f / 2.4f) - 0.055f;
  return static_cast<std::uint8_t>(std::clamp(srgb, 0.f, 1.f) * 255.f);
}

float material_hash(std::uint32_t x, std::uint32_t y) {
  std::uint32_t n = x * 374761393u + y * 668265263u;
  n = (n ^ (n >> 13)) * 1274126177u;
  n ^= n >> 16;
  return static_cast<float>(n & 0xFFFFu) / 65535.0f;
}

int wrap_period(int x, int period) {
  int r = x % period;
  if (r < 0) {
    r += period;
  }
  return r;
}

float value_noise(float x, float y, int period) {
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(x0);
  const float fy = y - static_cast<float>(y0);
  const float u = fx * fx * (3.f - 2.f * fx);
  const float v = fy * fy * (3.f - 2.f * fy);
  const float n00 = material_hash(static_cast<std::uint32_t>(wrap_period(x0, period)),
                                  static_cast<std::uint32_t>(wrap_period(y0, period)));
  const float n10 = material_hash(static_cast<std::uint32_t>(wrap_period(x0 + 1, period)),
                                  static_cast<std::uint32_t>(wrap_period(y0, period)));
  const float n01 = material_hash(static_cast<std::uint32_t>(wrap_period(x0, period)),
                                  static_cast<std::uint32_t>(wrap_period(y0 + 1, period)));
  const float n11 = material_hash(static_cast<std::uint32_t>(wrap_period(x0 + 1, period)),
                                  static_cast<std::uint32_t>(wrap_period(y0 + 1, period)));
  const float nx0 = n00 + (n10 - n00) * u;
  const float nx1 = n01 + (n11 - n01) * u;
  return nx0 + (nx1 - nx0) * v;
}

float fbm_uv(float u, float v, int base_cells, int octaves) {
  float sum = 0.f;
  float amp = 1.f;
  float norm = 0.f;
  int cells = base_cells;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * value_noise(u * static_cast<float>(cells), v * static_cast<float>(cells), cells);
    norm += amp;
    amp *= 0.5f;
    cells *= 2;
  }
  return sum / std::max(norm, 1e-6f);
}

template <typename Fn>
TextureAsset make_material_texture(Fn&& color_at) {
  TextureAsset texture;
  texture.width = kMaterialTextureSize;
  texture.height = kMaterialTextureSize;
  texture.srgb = true;
  texture.usage = TextureUsage::Albedo;
  texture.rgba.resize(static_cast<std::size_t>(kMaterialTextureSize) *
                      kMaterialTextureSize * 4);
  for (std::uint32_t y = 0; y < kMaterialTextureSize; ++y) {
    for (std::uint32_t x = 0; x < kMaterialTextureSize; ++x) {
      const Vec3 linear = color_at(x, y);
      const std::size_t i =
          (static_cast<std::size_t>(y) * kMaterialTextureSize + x) * 4;
      texture.rgba[i + 0] = linear_to_srgb_u8(linear.x);
      texture.rgba[i + 1] = linear_to_srgb_u8(linear.y);
      texture.rgba[i + 2] = linear_to_srgb_u8(linear.z);
      texture.rgba[i + 3] = 255;
    }
  }
  return texture;
}

template <typename HeightFn>
TextureAsset make_normal_texture(HeightFn&& height_at, float strength) {
  TextureAsset texture;
  texture.width = kMaterialTextureSize;
  texture.height = kMaterialTextureSize;
  texture.srgb = false;
  texture.usage = TextureUsage::Normal;
  texture.rgba.resize(static_cast<std::size_t>(kMaterialTextureSize) *
                      kMaterialTextureSize * 4);
  const auto wrap = [](int v) {
    const int n = static_cast<int>(kMaterialTextureSize);
    int r = v % n;
    if (r < 0) {
      r += n;
    }
    return static_cast<std::uint32_t>(r);
  };
  for (std::uint32_t y = 0; y < kMaterialTextureSize; ++y) {
    for (std::uint32_t x = 0; x < kMaterialTextureSize; ++x) {
      const float hL = height_at(wrap(static_cast<int>(x) - 1), y);
      const float hR = height_at(wrap(static_cast<int>(x) + 1), y);
      const float hD = height_at(x, wrap(static_cast<int>(y) - 1));
      const float hU = height_at(x, wrap(static_cast<int>(y) + 1));
      Vec3 n = normalize(Vec3{(hL - hR) * strength, (hD - hU) * strength, 1.f});
      const std::size_t i =
          (static_cast<std::size_t>(y) * kMaterialTextureSize + x) * 4;
      texture.rgba[i + 0] = static_cast<std::uint8_t>((n.x * 0.5f + 0.5f) * 255.f);
      texture.rgba[i + 1] = static_cast<std::uint8_t>((n.y * 0.5f + 0.5f) * 255.f);
      texture.rgba[i + 2] = static_cast<std::uint8_t>((n.z * 0.5f + 0.5f) * 255.f);
      texture.rgba[i + 3] = 255;
    }
  }
  return texture;
}

TextureAsset finish_builtin(TextureAsset texture, std::string_view key, std::string name) {
  texture.builtin_key = std::string(key);
  texture.name = std::move(name);
  texture.content_hash = texture_content_hash(texture);
  texture.generation = 1;
  return texture;
}

}  // namespace

const std::vector<std::string_view>& builtin_texture_keys() {
  static const std::vector<std::string_view> keys = {
      kBuiltinDefaultAlbedo,  kBuiltinConcreteAlbedo, kBuiltinSteelAlbedo,
      kBuiltinWoodAlbedo,     kBuiltinPlasterAlbedo,  kBuiltinDefaultNormal,
      kBuiltinConcreteNormal, kBuiltinWoodNormal,     kBuiltinSteelNormal,
      kBuiltinPlasterNormal,  kBuiltinGlassNormal,
  };
  return keys;
}

TextureAsset make_builtin_texture(std::string_view key) {
  if (key == kBuiltinDefaultAlbedo) {
    return finish_builtin(
        make_material_texture([](std::uint32_t x, std::uint32_t y) {
          const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
          const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
          const float mottling = fbm_uv(u, v, 12, 5);
          const float grain = material_hash(x, y);
          const float t = 0.72f + 0.04f * mottling + 0.018f * grain;
          return Vec3{t, t, t};
        }),
        key, "Default albedo");
  }
  if (key == kBuiltinConcreteAlbedo) {
    return finish_builtin(
        make_material_texture([](std::uint32_t x, std::uint32_t y) {
          const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
          const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
          const float mottling = fbm_uv(u, v, 10, 6);
          const float aggregate = value_noise(u * 64.f, v * 64.f, 64);
          const float sand = material_hash(x, y);
          const float t = 0.58f + 0.10f * mottling + 0.05f * aggregate + 0.035f * sand;
          return Vec3{t * 1.08f, t * 1.00f, t * 0.90f};
        }),
        key, "Concrete albedo");
  }
  if (key == kBuiltinSteelAlbedo) {
    return finish_builtin(
        make_material_texture([](std::uint32_t x, std::uint32_t y) {
          const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
          const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
          const float grain = material_hash(x, y);
          const float warp = 0.12f * fbm_uv(u, v, 16, 4);
          const float streak =
              0.5f + 0.5f * std::sin(2.f * kPi * (u * 72.f + warp) +
                                     0.35f * std::sin(2.f * kPi * v * 9.f));
          const float t = 0.54f + 0.035f * grain + 0.055f * streak;
          return Vec3{t, t, t};
        }),
        key, "Steel albedo");
  }
  if (key == kBuiltinWoodAlbedo) {
    return finish_builtin(
        make_material_texture([](std::uint32_t x, std::uint32_t y) {
          const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
          const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
          const float warp = 0.28f * fbm_uv(u, v, 8, 5);
          const float grain =
              std::sin(2.f * kPi * (u * 28.f + warp) +
                       1.35f * std::sin(2.f * kPi * v * 6.f)) +
              0.16f * std::sin(2.f * kPi * (u * 54.f + 0.4f * warp));
          const float pores = material_hash(x, y);
          const float shade = 0.78f + 0.14f * (0.5f + 0.5f * grain) + 0.04f * pores;
          return Vec3{0.58f * shade, 0.39f * shade, 0.22f * shade};
        }),
        key, "Wood albedo");
  }
  if (key == kBuiltinPlasterAlbedo) {
    return finish_builtin(
        make_material_texture([](std::uint32_t x, std::uint32_t y) {
          const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
          const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
          const float mottling = fbm_uv(u, v, 14, 5);
          const float grain = material_hash(x, y);
          const float t = 0.88f + 0.025f * mottling + 0.02f * grain;
          return Vec3{t, t * 0.995f, t * 0.98f};
        }),
        key, "Plaster albedo");
  }
  if (key == kBuiltinDefaultNormal) {
    return finish_builtin(
        make_normal_texture(
            [](std::uint32_t x, std::uint32_t y) {
              const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
              const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
              return fbm_uv(u, v, 12, 5) * 0.75f + material_hash(x, y) * 0.25f;
            },
            1.8f),
        key, "Default normal");
  }
  if (key == kBuiltinConcreteNormal) {
    return finish_builtin(
        make_normal_texture(
            [](std::uint32_t x, std::uint32_t y) {
              const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
              const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
              return fbm_uv(u, v, 10, 6) * 0.65f + value_noise(u * 64.f, v * 64.f, 64) * 0.35f;
            },
            4.f),
        key, "Concrete normal");
  }
  if (key == kBuiltinWoodNormal) {
    return finish_builtin(
        make_normal_texture(
            [](std::uint32_t x, std::uint32_t y) {
              const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
              const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
              const float warp = 0.28f * fbm_uv(u, v, 8, 5);
              return 0.5f + 0.5f * std::sin(2.f * kPi * (u * 28.f + warp));
            },
            3.5f),
        key, "Wood normal");
  }
  if (key == kBuiltinSteelNormal) {
    return finish_builtin(
        make_normal_texture(
            [](std::uint32_t x, std::uint32_t y) {
              const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
              const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
              const float warp = 0.12f * fbm_uv(u, v, 16, 4);
              return 0.5f + 0.5f * std::sin(2.f * kPi * (u * 72.f + warp));
            },
            1.6f),
        key, "Steel normal");
  }
  if (key == kBuiltinPlasterNormal) {
    return finish_builtin(
        make_normal_texture(
            [](std::uint32_t x, std::uint32_t y) {
              const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
              const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
              return fbm_uv(u, v, 14, 5) * 0.85f + material_hash(x, y) * 0.15f;
            },
            2.4f),
        key, "Plaster normal");
  }
  if (key == kBuiltinGlassNormal) {
    return finish_builtin(
        make_normal_texture(
            [](std::uint32_t x, std::uint32_t y) {
              const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
              const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
              return fbm_uv(u, v, 3, 3);
            },
            0.45f),
        key, "Glass normal");
  }
  return {};
}

namespace {

const std::unordered_map<std::uint64_t, std::string_view>& builtin_hash_catalog() {
  static std::once_flag once;
  static std::unordered_map<std::uint64_t, std::string_view> catalog;
  std::call_once(once, [] {
    for (const std::string_view key : builtin_texture_keys()) {
      TextureAsset baked = make_builtin_texture(key);
      catalog.emplace(baked.content_hash, key);
    }
  });
  return catalog;
}

}  // namespace

std::string_view builtin_key_for_hash(std::uint64_t content_hash) {
  if (content_hash == 0) {
    return {};
  }
  const auto& catalog = builtin_hash_catalog();
  const auto it = catalog.find(content_hash);
  return it == catalog.end() ? std::string_view{} : it->second;
}

bool hydrate_builtin_texture(TextureAsset& texture) {
  if (texture.builtin_key.empty() || !texture.rgba.empty()) {
    return false;
  }
  TextureAsset baked = make_builtin_texture(texture.builtin_key);
  if (baked.rgba.empty()) {
    return false;
  }
  texture.width = baked.width;
  texture.height = baked.height;
  texture.srgb = baked.srgb;
  texture.rgba = std::move(baked.rgba);
  texture.content_hash = baked.content_hash;
  if (texture.usage == TextureUsage::Unknown) {
    texture.usage = baked.usage;
  }
  if (texture.name.empty()) {
    texture.name = std::move(baked.name);
  }
  if (texture.generation == 0) {
    texture.generation = 1;
  }
  return true;
}

void stamp_builtin_key(TextureAsset& texture) {
  if (!texture.builtin_key.empty()) {
    return;
  }
  if (texture.content_hash == 0) {
    texture.content_hash = texture_content_hash(texture);
  }
  const std::string_view key = builtin_key_for_hash(texture.content_hash);
  if (!key.empty()) {
    texture.builtin_key = std::string(key);
  }
}

}  // namespace tamias
