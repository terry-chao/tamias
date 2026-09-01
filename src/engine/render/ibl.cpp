#include "engine/render/ibl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tamias {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr std::uint32_t kEnvSize = 64;
constexpr std::uint32_t kIrradianceSize = 32;
constexpr std::uint32_t kPrefilterSize = 64;
constexpr std::uint32_t kLutSize = 128;
constexpr std::uint32_t kIrradianceSamples = 32;
constexpr std::uint32_t kPrefilterSamples = 32;
constexpr std::uint32_t kLutSamples = 64;

std::uint16_t float_to_half(float value) {
  union {
    float f;
    std::uint32_t u;
  } conv;
  conv.f = value;
  const std::uint32_t x = conv.u;
  const std::uint32_t sign = (x >> 16) & 0x8000u;
  std::uint32_t mantissa = x & 0x7fffffu;
  int exponent = static_cast<int>((x >> 23) & 0xff) - 127 + 15;
  if (exponent <= 0) {
    if (exponent < -10) {
      return static_cast<std::uint16_t>(sign);
    }
    mantissa = (mantissa | 0x800000u) >> (1 - exponent);
    return static_cast<std::uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
  }
  if (exponent >= 31) {
    return static_cast<std::uint16_t>(sign | 0x7c00u);
  }
  return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) |
                                    ((mantissa + 0x1000u) >> 13));
}

void store_rgba16f(std::vector<std::uint16_t>& out, std::size_t pixel, Vec3 color) {
  const std::size_t i = pixel * 4;
  out[i + 0] = float_to_half(std::max(color.x, 0.f));
  out[i + 1] = float_to_half(std::max(color.y, 0.f));
  out[i + 2] = float_to_half(std::max(color.z, 0.f));
  out[i + 3] = float_to_half(1.f);
}

void store_rg16f(std::vector<std::uint16_t>& out, std::size_t pixel, float r, float g) {
  const std::size_t i = pixel * 2;
  out[i + 0] = float_to_half(std::clamp(r, 0.f, 1.f));
  out[i + 1] = float_to_half(std::clamp(g, 0.f, 1.f));
}

Vec3 cube_direction(int face, float u, float v) {
  switch (face) {
    case 0:
      return normalize(Vec3{1.f, -v, -u});
    case 1:
      return normalize(Vec3{-1.f, -v, u});
    case 2:
      return normalize(Vec3{u, 1.f, v});
    case 3:
      return normalize(Vec3{u, -1.f, -v});
    case 4:
      return normalize(Vec3{u, -v, 1.f});
    default:
      return normalize(Vec3{-u, -v, -1.f});
  }
}

Vec3 studio_radiance(Vec3 dir) {
  const Vec3 d = normalize(dir);
  const float hemi = std::clamp(d.y * 0.5f + 0.5f, 0.f, 1.f);
  Vec3 sky{0.10f + 0.18f * hemi, 0.12f + 0.22f * hemi, 0.14f + 0.32f * hemi};
  if (d.y < 0.f) {
    const float g = -d.y;
    sky = Vec3{0.08f + 0.04f * (1.f - g), 0.075f + 0.03f * (1.f - g), 0.07f + 0.02f * (1.f - g)};
  }
  const auto lobe = [](Vec3 direction, Vec3 axis, float power, float intensity) {
    const float nd = std::max(dot(direction, normalize(axis)), 0.f);
    return std::pow(nd, power) * intensity;
  };
  const float key = lobe(d, {0.35f, 0.72f, 0.48f}, 48.f, 6.5f);
  const float fill = lobe(d, {-0.55f, 0.28f, -0.35f}, 24.f, 1.8f);
  const float rim = lobe(d, {-0.15f, 0.15f, -0.85f}, 36.f, 1.2f);
  return {sky.x + key * 1.00f + fill * 0.55f + rim * 0.70f,
          sky.y + key * 0.97f + fill * 0.65f + rim * 0.80f,
          sky.z + key * 0.92f + fill * 0.95f + rim * 1.00f};
}

Vec3 sample_env(const std::vector<std::uint16_t> faces[6], std::uint32_t size, Vec3 dir) {
  const Vec3 d = normalize(dir);
  const float ax = std::fabs(d.x);
  const float ay = std::fabs(d.y);
  const float az = std::fabs(d.z);
  int face = 4;
  float sc = 0.f;
  float tc = 0.f;
  float ma = az;
  if (ax >= ay && ax >= az) {
    face = d.x >= 0.f ? 0 : 1;
    ma = ax;
    sc = d.x >= 0.f ? -d.z : d.z;
    tc = -d.y;
  } else if (ay >= ax && ay >= az) {
    face = d.y >= 0.f ? 2 : 3;
    ma = ay;
    sc = d.x;
    tc = d.y >= 0.f ? d.z : -d.z;
  } else {
    face = d.z >= 0.f ? 4 : 5;
    ma = az;
    sc = d.z >= 0.f ? d.x : -d.x;
    tc = -d.y;
  }
  const float u = 0.5f * (sc / std::max(ma, 1e-8f) + 1.f);
  const float v = 0.5f * (tc / std::max(ma, 1e-8f) + 1.f);
  const float fx = std::clamp(u * static_cast<float>(size) - 0.5f, 0.f, static_cast<float>(size - 1));
  const float fy = std::clamp(v * static_cast<float>(size) - 0.5f, 0.f, static_cast<float>(size - 1));
  const auto at = [&](std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 4;
    auto h2f = [](std::uint16_t h) {
      const std::uint32_t sign = (h & 0x8000u) << 16;
      std::uint32_t exponent = (h >> 10) & 0x1fu;
      std::uint32_t mantissa = h & 0x3ffu;
      union {
        float f;
        std::uint32_t u;
      } conv{};
      if (exponent == 0) {
        conv.u = sign;
        return conv.f;
      }
      exponent = exponent - 15 + 127;
      conv.u = sign | (exponent << 23) | (mantissa << 13);
      return conv.f;
    };
    return Vec3{h2f(faces[face][i]), h2f(faces[face][i + 1]), h2f(faces[face][i + 2])};
  };
  const auto x0 = static_cast<std::uint32_t>(fx);
  const auto y0 = static_cast<std::uint32_t>(fy);
  const std::uint32_t x1 = std::min(x0 + 1, size - 1);
  const std::uint32_t y1 = std::min(y0 + 1, size - 1);
  const float tx = fx - static_cast<float>(x0);
  const float ty = fy - static_cast<float>(y0);
  const Vec3 c00 = at(x0, y0);
  const Vec3 c10 = at(x1, y0);
  const Vec3 c01 = at(x0, y1);
  const Vec3 c11 = at(x1, y1);
  const Vec3 a = c00 + (c10 - c00) * tx;
  const Vec3 b = c01 + (c11 - c01) * tx;
  return a + (b - a) * ty;
}

std::uint32_t reverse_bits(std::uint32_t bits) {
  bits = (bits << 16) | (bits >> 16);
  bits = ((bits & 0x00ff00ffu) << 8) | ((bits & 0xff00ff00u) >> 8);
  bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits & 0xf0f0f0f0u) >> 4);
  bits = ((bits & 0x33333333u) << 2) | ((bits & 0xccccccccu) >> 2);
  bits = ((bits & 0x55555555u) << 1) | ((bits & 0xaaaaaaaau) >> 1);
  return bits;
}

Vec2 hammersley(std::uint32_t i, std::uint32_t n) {
  return {static_cast<float>(i) / static_cast<float>(n),
          static_cast<float>(reverse_bits(i)) * 2.3283064365386963e-10f};
}

void tangent_basis(Vec3 n, Vec3& t, Vec3& b) {
  const Vec3 up = std::fabs(n.y) < 0.999f ? Vec3{0.f, 1.f, 0.f} : Vec3{1.f, 0.f, 0.f};
  t = normalize(cross(up, n));
  b = cross(n, t);
}

Vec3 importance_sample_ggx(Vec2 xi, float roughness, Vec3 n) {
  const float a = roughness * roughness;
  const float phi = 2.f * kPi * xi.x;
  const float cos_theta = std::sqrt((1.f - xi.y) / (1.f + (a * a - 1.f) * xi.y));
  const float sin_theta = std::sqrt(std::max(1.f - cos_theta * cos_theta, 0.f));
  Vec3 h{std::cos(phi) * sin_theta, std::sin(phi) * sin_theta, cos_theta};
  Vec3 t;
  Vec3 b;
  tangent_basis(n, t, b);
  return normalize(t * h.x + b * h.y + n * h.z);
}

float geometry_schlick_ggx(float ndotv, float roughness) {
  const float k = (roughness * roughness) / 2.f;
  return ndotv / (ndotv * (1.f - k) + k);
}

float geometry_smith(float ndotv, float ndotl, float roughness) {
  return geometry_schlick_ggx(ndotv, roughness) * geometry_schlick_ggx(ndotl, roughness);
}

void fill_env_face(std::vector<std::uint16_t>& face, int face_index, std::uint32_t size) {
  face.assign(static_cast<std::size_t>(size) * size * 4, 0);
  for (std::uint32_t y = 0; y < size; ++y) {
    for (std::uint32_t x = 0; x < size; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.f - 1.f;
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.f - 1.f;
      store_rgba16f(face, static_cast<std::size_t>(y) * size + x,
                    studio_radiance(cube_direction(face_index, u, v)));
    }
  }
}

}  // namespace

IblCpu bake_studio_ibl() {
  IblCpu ibl;
  ibl.irradiance_size = kIrradianceSize;
  ibl.prefilter_size = kPrefilterSize;
  ibl.lut_size = kLutSize;

  std::vector<std::uint16_t> env[6];
  for (int face = 0; face < 6; ++face) {
    fill_env_face(env[face], face, kEnvSize);
  }

  for (int face = 0; face < 6; ++face) {
    auto& dst = ibl.irradiance_faces[face];
    dst.assign(static_cast<std::size_t>(kIrradianceSize) * kIrradianceSize * 4, 0);
    for (std::uint32_t y = 0; y < kIrradianceSize; ++y) {
      for (std::uint32_t x = 0; x < kIrradianceSize; ++x) {
        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kIrradianceSize) * 2.f - 1.f;
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kIrradianceSize) * 2.f - 1.f;
        const Vec3 n = cube_direction(face, u, v);
        Vec3 t;
        Vec3 b;
        tangent_basis(n, t, b);
        Vec3 acc{};
        for (std::uint32_t i = 0; i < kIrradianceSamples; ++i) {
          const Vec2 xi = hammersley(i, kIrradianceSamples);
          const float phi = 2.f * kPi * xi.x;
          const float cos_theta = std::sqrt(1.f - xi.y);
          const float sin_theta = std::sqrt(xi.y);
          const Vec3 l = normalize(t * (std::cos(phi) * sin_theta) + b * (std::sin(phi) * sin_theta) +
                                   n * cos_theta);
          acc += sample_env(env, kEnvSize, l) * cos_theta;
        }
        acc = acc * (kPi / static_cast<float>(kIrradianceSamples));
        store_rgba16f(dst, static_cast<std::size_t>(y) * kIrradianceSize + x, acc);
      }
    }
  }

  std::uint32_t mips = 1;
  std::uint32_t mip_size = kPrefilterSize;
  while (mip_size > 4) {
    mip_size /= 2;
    ++mips;
  }
  ibl.prefilter_mips = mips;
  for (int face = 0; face < 6; ++face) {
    ibl.prefilter_faces[face].resize(mips);
    for (std::uint32_t mip = 0; mip < mips; ++mip) {
      const std::uint32_t size = kPrefilterSize >> mip;
      const float roughness =
          mips > 1 ? static_cast<float>(mip) / static_cast<float>(mips - 1) : 0.f;
      auto& dst = ibl.prefilter_faces[face][mip];
      dst.assign(static_cast<std::size_t>(size) * size * 4, 0);
      for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
          const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.f - 1.f;
          const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.f - 1.f;
          const Vec3 n = cube_direction(face, u, v);
          const Vec3 vdir = n;
          Vec3 acc{};
          float weight = 0.f;
          if (roughness < 0.03f) {
            acc = sample_env(env, kEnvSize, n);
            weight = 1.f;
          } else {
            for (std::uint32_t i = 0; i < kPrefilterSamples; ++i) {
              const Vec3 h = importance_sample_ggx(hammersley(i, kPrefilterSamples), roughness, n);
              const Vec3 l = normalize(h * (2.f * dot(vdir, h)) - vdir);
              const float ndotl = std::max(dot(n, l), 0.f);
              if (ndotl > 0.f) {
                acc += sample_env(env, kEnvSize, l) * ndotl;
                weight += ndotl;
              }
            }
          }
          if (weight > 1e-5f) {
            acc = acc * (1.f / weight);
          }
          store_rgba16f(dst, static_cast<std::size_t>(y) * size + x, acc);
        }
      }
    }
  }

  ibl.brdf_lut.assign(static_cast<std::size_t>(kLutSize) * kLutSize * 2, 0);
  for (std::uint32_t y = 0; y < kLutSize; ++y) {
    const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(kLutSize);
    for (std::uint32_t x = 0; x < kLutSize; ++x) {
      const float ndotv = (static_cast<float>(x) + 0.5f) / static_cast<float>(kLutSize);
      const Vec3 v{std::sqrt(std::max(1.f - ndotv * ndotv, 0.f)), 0.f, ndotv};
      const Vec3 n{0.f, 0.f, 1.f};
      float a = 0.f;
      float b = 0.f;
      for (std::uint32_t i = 0; i < kLutSamples; ++i) {
        const Vec3 h = importance_sample_ggx(hammersley(i, kLutSamples), roughness, n);
        const Vec3 l = normalize(h * (2.f * dot(v, h)) - v);
        const float ndotl = std::max(l.z, 0.f);
        const float ndoth = std::max(h.z, 0.f);
        const float vdoth = std::max(dot(v, h), 0.f);
        if (ndotl > 0.f) {
          const float g = geometry_smith(ndotv, ndotl, roughness);
          const float g_vis = (g * vdoth) / std::max(ndoth * ndotv, 1e-4f);
          const float fc = std::pow(1.f - vdoth, 5.f);
          a += (1.f - fc) * g_vis;
          b += fc * g_vis;
        }
      }
      a /= static_cast<float>(kLutSamples);
      b /= static_cast<float>(kLutSamples);
      store_rg16f(ibl.brdf_lut, static_cast<std::size_t>(y) * kLutSize + x, a, b);
    }
  }

  return ibl;
}

}  // namespace tamias
