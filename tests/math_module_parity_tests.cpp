// C++20 模块试点回归测试：tamias.math 的取值必须和头文件路径一致，
// 并且符合若干手算的基准值（避免“两边一起错”）。

#include "math_module/parity_probe.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {

using tamias_math_probe::kProbeCount;

// 与 probe_body.inc 的 push 顺序一一对应。
enum Probe : std::size_t {
  kDot = 0,
  kUnitLength,
  kCrossZ,
  kWorldX,
  kWorldY,
  kWorldZ,
  kInvertError,
  kProj11,
  kProj22,
  kView23,
  kFrontVisible,
  kBehindCulled,
  kAabbT,
  kTriangleT,
  kSegmentT,
  kSnapX,
  kSnapZ,
  kOnGrid,
  kSnapRadius,
  kEyeX,
  kEyeY,
  kEyeZ,
  kViewMatrix22,
};

void expect_close(float got, float want, float tol) {
  EXPECT_NEAR(got, want, tol);
}

}  // namespace

TEST(MathModule, ProbeBodyFillsEverySlot) {
  const auto values = tamias_math_probe::probe_from_module();
  ASSERT_EQ(values.size(), kProbeCount);
  for (std::size_t i = 0; i < kProbeCount; ++i) {
    EXPECT_FALSE(std::isnan(values[i])) << "probe slot " << i << " was never filled";
  }
}

TEST(MathModule, ModuleMatchesHandComputedValues) {
  const auto p = tamias_math_probe::probe_from_module();

  expect_close(p[kDot], 32.f, 1e-5f);
  expect_close(p[kUnitLength], 1.f, 1e-5f);
  expect_close(p[kCrossZ], 1.f, 1e-6f);

  // translate({1,2,3}) * rotate_y(0.5) * scale(2) applied to (1,0,0)。
  expect_close(p[kWorldX], 1.f + 2.f * std::cos(0.5f), 1e-5f);
  expect_close(p[kWorldY], 2.f, 1e-5f);
  expect_close(p[kWorldZ], 3.f - 2.f * std::sin(0.5f), 1e-5f);
  EXPECT_LT(p[kInvertError], 1e-4f);

  expect_close(p[kProj11], 1.f / std::tan(0.4f), 1e-4f);
  expect_close(p[kView23], -5.f, 1e-5f);

  expect_close(p[kFrontVisible], 1.f, 0.f);
  expect_close(p[kBehindCulled], 0.f, 0.f);
  expect_close(p[kAabbT], 1.f, 1e-5f);
  expect_close(p[kTriangleT], 2.f, 1e-5f);
  expect_close(p[kSegmentT], 3.f, 1e-5f);

  expect_close(p[kSnapX], 1.f, 1e-6f);
  expect_close(p[kSnapZ], -3.f, 1e-6f);
  expect_close(p[kOnGrid], 1.f, 0.f);
  expect_close(p[kSnapRadius], 12.f * 2.f * std::tan(0.4f) * 10.f / 1080.f, 1e-5f);

  expect_close(p[kEyeX], 0.f, 1e-6f);
  expect_close(p[kEyeY], 0.f, 1e-6f);
  expect_close(p[kEyeZ], 5.f, 1e-6f);
  expect_close(p[kViewMatrix22], 1.f, 1e-6f);
}

TEST(MathModule, HeaderAndModuleAgree) {
  const std::string mismatch = tamias_math_probe::compare_probes();
  EXPECT_TRUE(mismatch.empty()) << mismatch;

  const auto h = tamias_math_probe::probe_from_headers();
  const auto m = tamias_math_probe::probe_from_module();
  for (std::size_t i = 0; i < kProbeCount; ++i) {
    EXPECT_FALSE(std::isnan(h[i])) << "header probe slot " << i << " was never filled";
  }
}
