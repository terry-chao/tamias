#include "engine/drawing/dxf_reader.h"

#include "engine/core/fs_utf8.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace tamias;

namespace {

// 把实体组码包成最小 DXF：只有 ENTITIES 段，够解析器用。
std::string dxf_with(const std::string& entities) {
  return "0\nSECTION\n2\nENTITIES\n" + entities + "0\nENDSEC\n0\nEOF\n";
}

std::string group(int code, std::string_view value) {
  return std::to_string(code) + "\n" + std::string(value) + "\n";
}

// 真实文件几乎都是 CRLF：\r 必须当成行尾噪声，而不是值的一部分。
std::string to_crlf(std::string_view text) {
  std::string out;
  out.reserve(text.size() + text.size() / 8);
  for (const char c : text) {
    if (c == '\n') {
      out += "\r\n";
    } else {
      out += c;
    }
  }
  return out;
}

Drawing must_parse(const std::string& text) {
  auto result = parse_dxf(text);
  EXPECT_TRUE(result.has_value()) << (result ? "" : result.error());
  return result ? std::move(*result) : Drawing{};
}

}  // namespace

TEST(DxfReader, ParsesLineIntoTwoPointPath) {
  std::string entities;
  entities += group(0, "LINE");
  entities += group(8, "WALL");
  entities += group(10, "0");
  entities += group(20, "0");
  entities += group(11, "100");
  entities += group(21, "50");

  const Drawing drawing = must_parse(dxf_with(entities));
  ASSERT_EQ(drawing.paths().size(), 1u);
  const DrawingPath& path = drawing.paths().front();
  ASSERT_EQ(path.points.size(), 2u);
  EXPECT_FALSE(path.closed);
  EXPECT_FLOAT_EQ(path.points[0].x, 0.f);
  EXPECT_FLOAT_EQ(path.points[1].y, 50.f);
  EXPECT_FLOAT_EQ(drawing.bounds().width(), 100.f);
  EXPECT_FLOAT_EQ(drawing.bounds().height(), 50.f);
  ASSERT_EQ(drawing.layers().size(), 1u);
  EXPECT_EQ(drawing.layers().front().name, "WALL");
}

TEST(DxfReader, CircleBecomesClosedPathStartingAtZero) {
  std::string entities;
  entities += group(0, "CIRCLE");
  entities += group(10, "10");
  entities += group(20, "20");
  entities += group(40, "5");

  const Drawing drawing = must_parse(dxf_with(entities));
  ASSERT_EQ(drawing.paths().size(), 1u);
  const DrawingPath& path = drawing.paths().front();
  EXPECT_TRUE(path.closed);
  EXPECT_GE(path.points.size(), 16u);
  // 圆心 (10,20) 半径 5 → 归一化后包围盒是 10×10。
  EXPECT_NEAR(drawing.bounds().width(), 10.f, 1e-3f);
  EXPECT_NEAR(drawing.bounds().height(), 10.f, 1e-3f);
  EXPECT_NEAR(drawing.bounds().min_x, 0.f, 1e-3f);
}

TEST(DxfReader, ArcSweepsCounterClockwiseFromStartToEnd) {
  std::string entities;
  entities += group(0, "ARC");
  entities += group(10, "0");
  entities += group(20, "0");
  entities += group(40, "10");
  entities += group(50, "0");
  entities += group(51, "90");

  const Drawing drawing = must_parse(dxf_with(entities));
  ASSERT_EQ(drawing.paths().size(), 1u);
  const DrawingPath& path = drawing.paths().front();
  EXPECT_FALSE(path.closed);
  ASSERT_GE(path.points.size(), 3u);
  // 起点 (10,0)，终点 (0,10)，都在归一化后的 (0..10) 方框里。
  EXPECT_NEAR(path.points.front().x, 10.f, 1e-3f);
  EXPECT_NEAR(path.points.front().y, 0.f, 1e-3f);
  EXPECT_NEAR(path.points.back().x, 0.f, 1e-3f);
  EXPECT_NEAR(path.points.back().y, 10.f, 1e-3f);
}

TEST(DxfReader, LwPolylineBulgeAddsArcPoints) {
  std::string entities;
  entities += group(0, "LWPOLYLINE");
  entities += group(90, "2");
  entities += group(70, "0");
  entities += group(10, "0");
  entities += group(20, "0");
  entities += group(42, "1");  // 半圆凸度（tan 45°）
  entities += group(10, "10");
  entities += group(20, "0");

  const Drawing drawing = must_parse(dxf_with(entities));
  ASSERT_EQ(drawing.paths().size(), 1u);
  const DrawingPath& path = drawing.paths().front();
  EXPECT_GT(path.points.size(), 2u);
  // 半径 5 的半圆：正凸度（逆时针）从 (0,0) 到 (10,0) 时向下鼓，矢高 = 半径 = 5。
  // 归一化会把最低点平移到 0，所以断言高度而不是 min_y。
  EXPECT_NEAR(drawing.bounds().height(), 5.f, 1e-2f);
}

TEST(DxfReader, ClosedLwPolylineSetsFlag) {
  std::string entities;
  entities += group(0, "LWPOLYLINE");
  entities += group(90, "3");
  entities += group(70, "1");
  entities += group(10, "0");
  entities += group(20, "0");
  entities += group(10, "10");
  entities += group(20, "0");
  entities += group(10, "10");
  entities += group(20, "10");

  const Drawing drawing = must_parse(dxf_with(entities));
  ASSERT_EQ(drawing.paths().size(), 1u);
  EXPECT_TRUE(drawing.paths().front().closed);
  ASSERT_GE(drawing.paths().front().points.size(), 4u);
  EXPECT_FLOAT_EQ(drawing.paths().front().points.front().x,
                  drawing.paths().front().points.back().x);
}

TEST(DxfReader, EntityColorOverridesLayerColor) {
  std::string entities;
  entities += group(0, "LINE");
  entities += group(8, "L1");
  entities += group(62, "1");  // 红
  entities += group(10, "0");
  entities += group(20, "0");
  entities += group(11, "1");
  entities += group(21, "1");

  const Drawing drawing = must_parse(dxf_with(entities));
  ASSERT_EQ(drawing.paths().size(), 1u);
  EXPECT_FLOAT_EQ(drawing.paths().front().color.x, 1.f);
  EXPECT_FLOAT_EQ(drawing.paths().front().color.y, 0.f);
}

TEST(DxfReader, TrueColorWinsOverAciIndex) {
  std::string entities;
  entities += group(0, "LINE");
  entities += group(62, "1");
  entities += group(420, "255");  // 0x0000FF → 蓝
  entities += group(10, "0");
  entities += group(20, "0");
  entities += group(11, "1");
  entities += group(21, "1");

  const Drawing drawing = must_parse(dxf_with(entities));
  ASSERT_EQ(drawing.paths().size(), 1u);
  EXPECT_FLOAT_EQ(drawing.paths().front().color.z, 1.f);
  EXPECT_FLOAT_EQ(drawing.paths().front().color.x, 0.f);
}

TEST(DxfReader, LayerTableColorFeedsByLayerEntities) {
  std::string text = "0\nSECTION\n2\nTABLES\n";
  text += group(0, "LAYER");
  text += group(2, "MARK");
  text += group(62, "5");  // 蓝
  text += "0\nENDSEC\n";
  text += "0\nSECTION\n2\nENTITIES\n";
  text += group(0, "LINE");
  text += group(8, "MARK");
  text += group(10, "0");
  text += group(20, "0");
  text += group(11, "1");
  text += group(21, "1");
  text += "0\nENDSEC\n0\nEOF\n";

  const Drawing drawing = must_parse(text);
  ASSERT_EQ(drawing.paths().size(), 1u);
  EXPECT_FLOAT_EQ(drawing.paths().front().color.z, 1.f);
  EXPECT_FLOAT_EQ(drawing.paths().front().color.x, 0.f);
}

TEST(DxfReader, TextEntitiesCarryPositionHeightAndString) {
  std::string entities;
  entities += group(0, "TEXT");
  entities += group(10, "3");
  entities += group(20, "4");
  entities += group(40, "2.5");
  entities += group(50, "30");
  entities += group(1, "GP-01");
  entities += group(0, "MTEXT");
  entities += group(10, "0");
  entities += group(20, "10");
  entities += group(40, "3");
  entities += group(1, "A\\P B");

  const Drawing drawing = must_parse(dxf_with(entities));
  ASSERT_EQ(drawing.texts().size(), 2u);
  EXPECT_EQ(drawing.texts()[0].text, "GP-01");
  EXPECT_NEAR(drawing.texts()[0].height, 2.5f, 1e-4f);
  EXPECT_NEAR(drawing.texts()[0].rotation_deg, 30.f, 1e-4f);
  EXPECT_EQ(drawing.texts()[1].text, std::string("A\n B"));
}

TEST(DxfReader, InsertAppliesBlockTranslationAndScale) {
  std::string text = "0\nSECTION\n2\nBLOCKS\n";
  text += group(0, "BLOCK");
  text += group(2, "BOLT");
  text += group(10, "0");
  text += group(20, "0");
  text += group(0, "LINE");
  text += group(10, "0");
  text += group(20, "0");
  text += group(11, "1");
  text += group(21, "0");
  text += group(0, "ENDBLK");
  text += "0\nENDSEC\n";
  text += "0\nSECTION\n2\nENTITIES\n";
  text += group(0, "INSERT");
  text += group(2, "BOLT");
  text += group(10, "100");
  text += group(20, "200");
  text += group(41, "2");
  text += group(42, "2");
  text += "0\nENDSEC\n0\nEOF\n";

  const Drawing drawing = must_parse(text);
  ASSERT_EQ(drawing.paths().size(), 1u);
  // 平移 + 2 倍缩放后归一化到原点：长度 2。
  EXPECT_NEAR(drawing.bounds().width(), 2.f, 1e-4f);
  EXPECT_NEAR(drawing.bounds().min_x, 0.f, 1e-4f);
}

TEST(DxfReader, NormalizeOriginKeepsWorldOffset) {
  std::string entities;
  entities += group(0, "LINE");
  entities += group(10, "1000000");
  entities += group(20, "500000");
  entities += group(11, "1000010");
  entities += group(21, "500000");

  const Drawing drawing = must_parse(dxf_with(entities));
  EXPECT_FLOAT_EQ(drawing.world_origin().x, 1000000.f);
  EXPECT_FLOAT_EQ(drawing.world_origin().y, 500000.f);
  EXPECT_FLOAT_EQ(drawing.bounds().min_x, 0.f);
  EXPECT_FLOAT_EQ(drawing.bounds().max_x, 10.f);
}

TEST(DxfReader, UnsupportedEntitiesAreCountedNotFatal) {
  std::string entities;
  entities += group(0, "SPLINE");
  entities += group(8, "S");
  entities += group(0, "HATCH");
  entities += group(8, "H");
  entities += group(0, "LINE");
  entities += group(10, "0");
  entities += group(20, "0");
  entities += group(11, "1");
  entities += group(21, "1");

  const Drawing drawing = must_parse(dxf_with(entities));
  EXPECT_EQ(drawing.paths().size(), 1u);
  EXPECT_EQ(drawing.unsupported_entity_count(), 2u);
}

TEST(DxfReader, RejectsBinaryDxfAndGarbage) {
  auto binary = parse_dxf("AutoCAD Binary DXF\r\n\x1a\x00");
  ASSERT_FALSE(binary.has_value());
  EXPECT_NE(binary.error().find("binary DXF"), std::string::npos);

  auto garbage = parse_dxf("hello world\nnot a dxf\n");
  EXPECT_FALSE(garbage.has_value());

  auto empty_entities = parse_dxf(dxf_with(""));
  ASSERT_FALSE(empty_entities.has_value());
}

// 回归：Windows 上 std::filesystem::path(std::string) 走 ANSI 代码页，含中文的
// 文件名必须在引擎里也能打开（app 侧用 qstring_to_path 构造，这里直接给宽字符路径）。
TEST(DxfReader, LoadsFileWithNonAsciiName) {
  std::string entities;
  entities += group(0, "LINE");
  entities += group(10, "0");
  entities += group(20, "0");
  entities += group(11, "10");
  entities += group(21, "10");

  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      std::filesystem::path(L"tamias-\u56fe\u7eb8-\u6d4b\u8bd5.dxf");
  {
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good());
    out << dxf_with(entities);
  }
  auto loaded = load_dxf(path);
  ASSERT_TRUE(loaded.has_value()) << (loaded ? "" : loaded.error());
  EXPECT_EQ(loaded->paths().size(), 1u);

  auto missing = load_dxf(std::filesystem::temp_directory_path() /
                          std::filesystem::path(L"tamias-\u4e0d\u5b58\u5728.dxf"));
  ASSERT_FALSE(missing.has_value());
  EXPECT_NE(missing.error().find("cannot open DXF"), std::string::npos);

  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

// 回归：解析失败时要用 UTF-8 拼错误信息。之前用 path.filename().string()，
// 宽→窄走 ANSI 代码页，中文文件名在报错这一步就把 std::system_error 抛出来了。
TEST(DxfReader, ParseFailureWithNonAsciiNameReportsUtf8Message) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      std::filesystem::path(L"tamias-\u65e0\u6548\u56fe\u7eb8.dxf");
  {
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good());
    out << "this is not a dxf\n";
  }
  auto loaded = load_dxf(path);
  ASSERT_FALSE(loaded.has_value());
  const std::string name = path_to_utf8(path.filename());
  EXPECT_NE(loaded.error().find(name), std::string::npos) << loaded.error();

  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

// 回归：整份文件是 CRLF。之前只在数字解析里 trim，字符串比较会带着 '\r'，
// 于是 "SECTION\r" != "SECTION"，ENTITIES 段被整个跳过 → "没有图元"。
TEST(DxfReader, ParsesCrlfFile) {
  std::string entities;
  entities += group(0, "LINE");
  entities += group(8, "WALL");
  entities += group(10, "0");
  entities += group(20, "0");
  entities += group(11, "10");
  entities += group(21, "10");
  entities += group(0, "LWPOLYLINE");
  entities += group(90, "2");
  entities += group(70, "1");
  entities += group(10, "0");
  entities += group(20, "0");
  entities += group(10, "5");
  entities += group(20, "5");

  const Drawing drawing = must_parse(to_crlf(dxf_with(entities)));
  EXPECT_EQ(drawing.paths().size(), 2u);
  EXPECT_TRUE(drawing.paths()[1].closed);
  // LINE 在 WALL 层，LWPOLYLINE 没写组码 8 → 落到默认层 "0"。
  ASSERT_EQ(drawing.layers().size(), 2u);
  EXPECT_EQ(drawing.layers().front().name, "WALL");
  EXPECT_EQ(drawing.layers().back().name, "0");
}

// 回归：UTF-8 BOM + CRLF（Windows 工具常这么写）。
TEST(DxfReader, ParsesUtf8BomWithCrlf) {
  std::string entities;
  entities += group(0, "CIRCLE");
  entities += group(10, "0");
  entities += group(20, "0");
  entities += group(40, "3");
  const std::string text = std::string("\xEF\xBB\xBF") + to_crlf(dxf_with(entities));
  const Drawing drawing = must_parse(text);
  ASSERT_EQ(drawing.paths().size(), 1u);
  EXPECT_TRUE(drawing.paths().front().closed);
}

TEST(DxfReader, RejectsUtf16WithClearMessage) {
  const std::string text = std::string("\xFF\xFE") + std::string("\x30\x00\x0A\x00", 4);
  auto loaded = parse_dxf(text);
  ASSERT_FALSE(loaded.has_value());
  EXPECT_NE(loaded.error().find("UTF-16"), std::string::npos) << loaded.error();
}

// 只有不支持的实体时，错误信息要说明是什么，而不是含糊的“没有图元”。
TEST(DxfReader, UnsupportedOnlyFileNamesTheEntityTypes) {
  std::string entities;
  for (int i = 0; i < 3; ++i) {
    entities += group(0, "SPLINE");
    entities += group(8, "S");
  }
  entities += group(0, "HATCH");
  entities += group(8, "H");
  auto loaded = parse_dxf(to_crlf(dxf_with(entities)));
  ASSERT_FALSE(loaded.has_value());
  EXPECT_NE(loaded.error().find("SPLINE \xC3\x97" "3"), std::string::npos) << loaded.error();
  EXPECT_NE(loaded.error().find("HATCH"), std::string::npos) << loaded.error();
}
