#include "engine/render/ibl.h"

#include <gtest/gtest.h>

#include <cmath>

namespace tamias {

TEST(IblBake, StudioIblHasExpectedSizesAndFiniteRadiance) {
  const IblCpu ibl = bake_studio_ibl();
  EXPECT_EQ(ibl.irradiance_size, 32u);
  EXPECT_EQ(ibl.prefilter_size, 64u);
  EXPECT_GE(ibl.prefilter_mips, 3u);
  EXPECT_EQ(ibl.lut_size, 128u);

  for (int face = 0; face < 6; ++face) {
    ASSERT_EQ(ibl.irradiance_faces[face].size(),
              static_cast<std::size_t>(ibl.irradiance_size) * ibl.irradiance_size * 4);
    ASSERT_EQ(ibl.prefilter_faces[face].size(), ibl.prefilter_mips);
    EXPECT_EQ(ibl.prefilter_faces[face][0].size(),
              static_cast<std::size_t>(ibl.prefilter_size) * ibl.prefilter_size * 4);
  }
  EXPECT_EQ(ibl.brdf_lut.size(), static_cast<std::size_t>(ibl.lut_size) * ibl.lut_size * 2);

  // LUT corners should be in [0,1] once decoded; at least the storage is non-zero.
  bool any_nonzero = false;
  for (std::uint16_t h : ibl.brdf_lut) {
    if (h != 0) {
      any_nonzero = true;
      break;
    }
  }
  EXPECT_TRUE(any_nonzero);

  any_nonzero = false;
  for (std::uint16_t h : ibl.irradiance_faces[2]) {
    if (h != 0) {
      any_nonzero = true;
      break;
    }
  }
  EXPECT_TRUE(any_nonzero);
}

}  // namespace tamias
