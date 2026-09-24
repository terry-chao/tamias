#include "bim/grid_dimensions.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tamias {
namespace {

GridAxis make_axis(std::uint64_t id, std::string name, GridAxisDirection direction, double position,
                   double start, double end) {
  GridAxis axis{};
  axis.id = id;
  axis.name = std::move(name);
  axis.direction = direction;
  axis.position = position;
  axis.start = start;
  axis.end = end;
  return axis;
}

TEST(GridDimensions, ChainsAdjacentAxesAlongTheirNormal) {
  std::vector<GridAxis> axes;
  axes.push_back(make_axis(1, "1", GridAxisDirection::AlongZ, 0.0, 0.0, 10.0));
  axes.push_back(make_axis(2, "2", GridAxisDirection::AlongZ, 6.0, 0.0, 10.0));
  axes.push_back(make_axis(3, "3", GridAxisDirection::AlongZ, 12.0, 0.0, 10.0));

  const std::vector<GridDimension> chain =
      grid_dimension_chain(axes, GridAxisDirection::AlongZ);
  ASSERT_EQ(chain.size(), 2u);
  EXPECT_EQ(chain[0].from_name, "1");
  EXPECT_EQ(chain[0].to_name, "2");
  EXPECT_DOUBLE_EQ(chain[0].distance, 6.0);
  // 锚点在两轴中间、轴网外缘（z = 10），y 恒为 0。
  EXPECT_FLOAT_EQ(chain[0].anchor.x, 3.f);
  EXPECT_FLOAT_EQ(chain[0].anchor.y, 0.f);
  EXPECT_FLOAT_EQ(chain[0].anchor.z, 10.f);
  EXPECT_FLOAT_EQ(chain[1].anchor.x, 9.f);
}

TEST(GridDimensions, SortsAxesByPosition) {
  std::vector<GridAxis> axes;
  axes.push_back(make_axis(3, "C", GridAxisDirection::AlongZ, 12.0, 0.0, 8.0));
  axes.push_back(make_axis(1, "A", GridAxisDirection::AlongZ, 0.0, 0.0, 8.0));
  axes.push_back(make_axis(2, "B", GridAxisDirection::AlongZ, 5.0, 0.0, 8.0));

  const std::vector<GridDimension> chain =
      grid_dimension_chain(axes, GridAxisDirection::AlongZ);
  ASSERT_EQ(chain.size(), 2u);
  EXPECT_EQ(chain[0].from_name, "A");
  EXPECT_EQ(chain[0].to_name, "B");
  EXPECT_DOUBLE_EQ(chain[0].distance, 5.0);
  EXPECT_DOUBLE_EQ(chain[1].distance, 7.0);
}

TEST(GridDimensions, IgnoresTheOtherDirectionAndAnchorsOnXForAlongX) {
  std::vector<GridAxis> axes;
  axes.push_back(make_axis(1, "A", GridAxisDirection::AlongX, 0.0, 0.0, 20.0));
  axes.push_back(make_axis(2, "B", GridAxisDirection::AlongX, 4.0, 0.0, 20.0));
  axes.push_back(make_axis(3, "1", GridAxisDirection::AlongZ, 7.0, 0.0, 30.0));

  const std::vector<GridDimension> chain =
      grid_dimension_chain(axes, GridAxisDirection::AlongX);
  ASSERT_EQ(chain.size(), 1u);
  EXPECT_DOUBLE_EQ(chain[0].distance, 4.0);
  // 沿 x 的横轴：链摆在 x 外缘（20），锚点落在 z 方向的两轴中间。
  EXPECT_FLOAT_EQ(chain[0].anchor.x, 20.f);
  EXPECT_FLOAT_EQ(chain[0].anchor.z, 2.f);
}

TEST(GridDimensions, NeedsAtLeastTwoAxes) {
  std::vector<GridAxis> single;
  single.push_back(make_axis(1, "1", GridAxisDirection::AlongZ, 0.0, 0.0, 5.0));
  EXPECT_TRUE(grid_dimension_chain(single, GridAxisDirection::AlongZ).empty());
  EXPECT_TRUE(grid_dimension_chain({}, GridAxisDirection::AlongZ).empty());
}

}  // namespace
}  // namespace tamias
