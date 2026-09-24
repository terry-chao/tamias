#include "bim/length_text.h"

#include <gtest/gtest.h>

namespace tamias {
namespace {

TEST(LengthText, FormatsElevationsWithSign) {
  EXPECT_EQ(format_elevation(0.0), "±0.000");
  EXPECT_EQ(format_elevation(3.6), "+3.600");
  EXPECT_EQ(format_elevation(-1.2), "-1.200");
  EXPECT_EQ(format_elevation(12.3456), "+12.346");  // 四舍五入到毫米
}

TEST(LengthText, RoundsToMillimetresBeforeSigning) {
  // 0.4 mm 以下不能显示成 "-0.000"——那在图纸上会被当成负标高。
  EXPECT_EQ(format_elevation(0.0004), "±0.000");
  EXPECT_EQ(format_elevation(-0.0004), "±0.000");
  EXPECT_EQ(format_elevation(-0.0006), "-0.001");
}

TEST(LengthText, FormatsDistancesWithoutSign) {
  EXPECT_EQ(format_distance(6.0), "6.000");
  EXPECT_EQ(format_distance(0.25), "0.250");
  EXPECT_EQ(format_distance(12.3456), "12.346");
}

}  // namespace
}  // namespace tamias
