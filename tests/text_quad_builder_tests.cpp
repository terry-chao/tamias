#include "engine/render/text/glyph_atlas.h"
#include "engine/document/picking.h"
#include "engine/render/text/font_search.h"
#include "engine/render/text/stb_font.h"
#include "engine/render/text/text_layout.h"
#include "engine/render/text/text_quad_builder.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace tamias {
namespace {

// 假字体：每个可见字都是 6 × 8 的实心块，空格没有墨迹。
class BoxRasterizer final : public GlyphRasterizer {
 public:
  [[nodiscard]] GlyphBitmap rasterize(std::uint32_t codepoint, float) const override {
    GlyphBitmap bitmap{};
    if (codepoint == U' ') {
      return bitmap;
    }
    bitmap.width = 6;
    bitmap.height = 8;
    bitmap.bearing_x = 0.f;
    bitmap.bearing_y = 8.f;
    bitmap.alpha.assign(48, 255);
    return bitmap;
  }
};

// 布局度量：拉丁 6 px、空格 6 px，行高 10、基线 8（size = 10）。
class Metrics final : public GlyphMetricsProvider {
 public:
  [[nodiscard]] GlyphMetrics metrics(std::uint32_t codepoint, float) const override {
    GlyphMetrics glyph{};
    glyph.valid = true;
    glyph.whitespace = codepoint == U' ';
    glyph.advance = 6.f;
    glyph.width = 6.f;
    glyph.height = 8.f;
    glyph.bearing_y = 8.f;
    return glyph;
  }
  [[nodiscard]] float line_height(float) const override { return 10.f; }
  [[nodiscard]] float ascent(float) const override { return 8.f; }
};

TEST(TextQuadBuilder, EmitsOneQuadPerVisibleGlyph) {
  const Metrics metrics;
  const BoxRasterizer rasterizer;
  GlyphAtlas atlas(64, 64, 0);
  TextStyle style{};
  style.size = 10.f;
  style.line_spacing = 1.f;
  const TextLayout layout = layout_text("AB", style, TextAlign::Left, 0.f, metrics);
  ASSERT_EQ(layout.lines.size(), 1u);

  std::vector<TextQuad> quads;
  const std::size_t added =
      append_text_quads(layout, 7, 100.f, 50.f, 10.f, Vec3{0.5f, 0.6f, 0.7f}, 0.8f, rasterizer,
                        atlas, 1, quads);
  EXPECT_EQ(added, 2u);
  ASSERT_EQ(quads.size(), 2u);

  // 笔位：第一个字 (100, 50)，第二个字往右一个 advance。
  EXPECT_FLOAT_EQ(quads[0].x, 100.f);
  EXPECT_FLOAT_EQ(quads[1].x, 106.f);
  // 基线 8、位图顶在基线上 8 → 四边形顶边正好落在原点。
  EXPECT_FLOAT_EQ(quads[0].y, 50.f);
  EXPECT_FLOAT_EQ(quads[0].width, 6.f);
  EXPECT_FLOAT_EQ(quads[0].height, 8.f);
  EXPECT_FLOAT_EQ(quads[0].color[0], 0.5f);
  EXPECT_FLOAT_EQ(quads[0].color[3], 0.8f);
  EXPECT_GE(quads[0].u0, 0.f);
  EXPECT_LE(quads[0].u1, 1.f);
  EXPECT_LT(quads[0].u0, quads[0].u1);
}

TEST(TextQuadBuilder, SkipsWhitespaceGlyphs) {
  const Metrics metrics;
  const BoxRasterizer rasterizer;
  GlyphAtlas atlas(64, 64, 0);
  TextStyle style{};
  style.size = 10.f;
  style.line_spacing = 1.f;
  const TextLayout layout = layout_text("A B", style, TextAlign::Left, 0.f, metrics);

  std::vector<TextQuad> quads;
  const std::size_t added = append_text_quads(layout, 1, 0.f, 0.f, 10.f, Vec3{1.f, 1.f, 1.f}, 1.f,
                                              rasterizer, atlas, 1, quads);
  EXPECT_EQ(added, 2u);  // 空格不画，但两边都画
  ASSERT_EQ(quads.size(), 2u);
  EXPECT_LT(quads[0].x, quads[1].x);
}

TEST(TextQuadBuilder, ReusesAtlasEntriesAcrossCalls) {
  const Metrics metrics;
  const BoxRasterizer rasterizer;
  GlyphAtlas atlas(64, 64, 0);
  TextStyle style{};
  style.size = 10.f;
  style.line_spacing = 1.f;
  const TextLayout layout = layout_text("AB", style, TextAlign::Left, 0.f, metrics);

  std::vector<TextQuad> first;
  std::vector<TextQuad> second;
  append_text_quads(layout, 1, 0.f, 0.f, 10.f, Vec3{1.f, 1.f, 1.f}, 1.f, rasterizer, atlas, 1,
                    first);
  append_text_quads(layout, 1, 40.f, 0.f, 10.f, Vec3{1.f, 1.f, 1.f}, 1.f, rasterizer, atlas, 2,
                    second);
  EXPECT_EQ(atlas.glyph_count(), 2u);  // 同一批字形，第二次只是复用
  ASSERT_EQ(first.size(), second.size());
  EXPECT_FLOAT_EQ(first[0].u0, second[0].u0);
  EXPECT_FLOAT_EQ(second[0].x, 40.f);  // 位置变了，UV 没变
}

TEST(TextQuadBuilder, AppendsToExistingQuads) {
  const Metrics metrics;
  const BoxRasterizer rasterizer;
  GlyphAtlas atlas(64, 64, 0);
  TextStyle style{};
  style.size = 10.f;
  style.line_spacing = 1.f;
  const TextLayout layout = layout_text("A", style, TextAlign::Left, 0.f, metrics);

  std::vector<TextQuad> quads;
  append_text_quads(layout, 1, 0.f, 0.f, 10.f, Vec3{1.f, 1.f, 1.f}, 1.f, rasterizer, atlas, 1,
                    quads);
  const std::size_t added = append_text_quads(layout, 1, 20.f, 0.f, 10.f, Vec3{1.f, 1.f, 1.f},
                                              1.f, rasterizer, atlas, 2, quads);
  EXPECT_EQ(added, 1u);
  EXPECT_EQ(quads.size(), 2u);
}

// 完整链条：世界锚点 → 投影到屏幕 → 排版 → 图集装箱 → 四边形。
// 用系统真字体（找不到就跳过），所以这条覆盖的是「轴号会不会画在轴线端头」。
TEST(TextQuadBuilder, PlacesAxisLabelAtProjectedAnchor) {
  const std::optional<FontCandidate> candidate = pick_default_font(default_font_dirs());
  if (!candidate.has_value()) {
    GTEST_SKIP() << "系统里找不到可用字体";
  }
  auto loaded = StbFont::load_file(candidate->path, candidate->face_index);
  if (!loaded) {
    GTEST_SKIP() << "字体加载失败";
  }
  const std::shared_ptr<StbFont> font = std::move(*loaded);

  // 单位投影 + 原点 → 屏幕正中心（800 × 600）。
  float sx = 0.f;
  float sy = 0.f;
  ASSERT_TRUE(project_world_to_screen(Mat4::identity(), Vec3{0.f, 0.f, 0.f}, 800.f, 600.f, sx, sy));
  EXPECT_FLOAT_EQ(sx, 400.f);
  EXPECT_FLOAT_EQ(sy, 300.f);

  TextStyle style{};
  style.size = 16.f;
  style.line_spacing = 1.f;
  const TextLayout layout = layout_text("1A", style, TextAlign::Left, 0.f, *font);
  ASSERT_FALSE(layout.empty());

  GlyphAtlas atlas(128, 256, 1);
  std::vector<TextQuad> quads;
  const std::size_t added =
      append_text_quads(layout, font->id(), sx, sy - layout.first_baseline, 16.f,
                        Vec3{1.f, 1.f, 1.f}, 1.f, *font, atlas, 1, quads);
  EXPECT_EQ(added, 2u);
  ASSERT_EQ(quads.size(), 2u);

  // 第一个字形压在锚点上，且在基线之上。
  EXPECT_NEAR(quads[0].x, sx, 6.f);
  EXPECT_LT(quads[0].y + quads[0].height, sy + 1.f);
  EXPECT_GT(quads[0].y, sy - 24.f);
  EXPECT_GT(quads[1].x, quads[0].x);  // 第二个字往右走
  for (const TextQuad& quad : quads) {
    EXPECT_GT(quad.width, 0.f);
    EXPECT_GT(quad.height, 0.f);
    EXPECT_GE(quad.u0, 0.f);
    EXPECT_LE(quad.u1, 1.f);
  }
}

}  // namespace
}  // namespace tamias
