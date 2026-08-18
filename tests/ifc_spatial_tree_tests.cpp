#include "bim/ifc_spatial_tree.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using namespace tamias;

namespace {

std::filesystem::path sample_ifc() {
  return std::filesystem::path(TAMIAS_SOURCE_DIR) / "assets" / "samples" / "spatial-tree.ifc";
}

}  // namespace

TEST(IfcSpatialTree, SampleProjectSiteBuildingStoreyWall) {
  auto tree = format_ifc_spatial_tree(sample_ifc());
  ASSERT_TRUE(tree) << tree.error();
  const std::string& text = *tree;
  EXPECT_NE(text.find("IfcProject"), std::string::npos);
  EXPECT_NE(text.find("Demo Project"), std::string::npos);
  EXPECT_NE(text.find("IfcSite"), std::string::npos);
  EXPECT_NE(text.find("Demo Site"), std::string::npos);
  EXPECT_NE(text.find("IfcBuilding"), std::string::npos);
  EXPECT_NE(text.find("Demo Building"), std::string::npos);
  EXPECT_NE(text.find("IfcBuildingStorey"), std::string::npos);
  EXPECT_NE(text.find("Level 1"), std::string::npos);
  EXPECT_NE(text.find("IfcWall"), std::string::npos);
  EXPECT_NE(text.find("Wall A"), std::string::npos);
}

TEST(IfcSpatialTree, MissingFile) {
  auto tree = format_ifc_spatial_tree(std::filesystem::path(TAMIAS_SOURCE_DIR) / "missing.ifc");
  ASSERT_FALSE(tree);
  EXPECT_NE(tree.error().find("not found"), std::string::npos);
}
