#pragma once

#include "engine/math/math.h"

#include <cstdint>
#include <vector>

namespace tamias {

// CPU-side split-sum IBL: studio HDRI → cubemap / irradiance / GGX prefilter / BRDF LUT.
struct IblCpu {
  std::uint32_t irradiance_size = 0;
  std::uint32_t prefilter_size = 0;
  std::uint32_t prefilter_mips = 0;
  std::uint32_t lut_size = 0;
  // RGBA16F, 6 faces. Each face is size*size*4 half-floats.
  std::vector<std::uint16_t> irradiance_faces[6];
  // prefilter_mips entries per face, mip0 = prefilter_size.
  std::vector<std::vector<std::uint16_t>> prefilter_faces[6];
  // RG16F, lut_size * lut_size * 2 half-floats.
  std::vector<std::uint16_t> brdf_lut;
};

IblCpu bake_studio_ibl();

}  // namespace tamias
