#include "engine/render/text/font_search.h"
#include "engine/render/text/font_fallback.h"
#include "engine/render/text/stb_font.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <vector>

namespace tamias {
namespace {

// 本机系统字体。CI / 精简镜像里可能一个都没有，那就跳过——这些用例测的是
// 「stb 这条解析路径通不通」，不是「这台机器有没有装字体」。
std::shared_ptr<StbFont> load_default_font() {
  const std::optional<FontCandidate> candidate = pick_default_font(default_font_dirs());
  if (!candidate.has_value()) {
    return nullptr;
  }
  Result<std::shared_ptr<StbFont>> font =
      StbFont::load_file(candidate->path, candidate->face_index);
  if (!font.has_value()) {
    return nullptr;
  }
  return std::move(*font);
}

std::shared_ptr<StbFont> load_font(const std::optional<FontCandidate>& candidate) {
  if (!candidate.has_value()) {
    return nullptr;
  }
  Result<std::shared_ptr<StbFont>> font =
      StbFont::load_file(candidate->path, candidate->face_index);
  if (!font.has_value()) {
    return nullptr;
  }
  return std::move(*font);
}

// 中文回落：拉丁字体认不全「一层」，得换成 CJK 字体——这个选择必须是可测的一步。
TEST(FontFallback, PicksTheFontThatCoversTheText) {
  const std::vector<std::filesystem::path> dirs = default_font_dirs();
  std::vector<std::shared_ptr<StbFont>> fonts;
  if (std::shared_ptr<StbFont> primary = load_font(pick_default_font(dirs));
      primary != nullptr) {
    fonts.push_back(std::move(primary));
  }
  if (fonts.empty()) {
    GTEST_SKIP() << "系统里找不到可用字体";
  }
  if (std::shared_ptr<StbFont> cjk = load_font(pick_cjk_font(dirs));
      cjk != nullptr && cjk->id() != fonts.front()->id()) {
    fonts.push_back(std::move(cjk));
  }

  EXPECT_EQ(pick_font_for_text("", fonts), nullptr);
  // 纯 ASCII：交出来的那套必须真有这些字。
  const StbFont* ascii = pick_font_for_text("F1", fonts);
  ASSERT_NE(ascii, nullptr);
  EXPECT_TRUE(ascii->has_glyph(U'F'));

  if (fonts.size() < 2) {
    GTEST_SKIP() << "没有中文回落字体（装了 msyh / Noto Sans CJK 才测得到）";
  }
  const StbFont* chinese = pick_font_for_text("一层", fonts);
  ASSERT_NE(chinese, nullptr);
  EXPECT_TRUE(chinese->has_glyph(0x4E00));  // 一
  EXPECT_TRUE(chinese->has_glyph(0x5C42));  // 层
  if (!fonts.front()->has_glyph(0x4E00)) {
    EXPECT_NE(chinese, fonts.front().get());  // 主字体认不了 → 确实回落了
  }
  // 谁都认不了的码位：如实返回 nullptr，让调用方自己决定画豆腐块还是跳过。
  EXPECT_EQ(pick_font_for_text("\xF4\x8F\xBF\xBF", fonts), nullptr);
}

TEST(FontSearch, FindsPlatformFontDirectories) {
  EXPECT_FALSE(default_font_dirs().empty());
}

TEST(FontSearch, ListsSortedFontFiles) {
  const std::vector<std::filesystem::path> dirs = default_font_dirs();
  const std::vector<std::filesystem::path> files = list_font_files(dirs);
  if (files.empty()) {
    GTEST_SKIP() << "系统字体目录里没有字体文件";
  }
  EXPECT_TRUE(std::is_sorted(files.begin(), files.end()));
  for (const std::filesystem::path& file : files) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    EXPECT_TRUE(ext == ".ttf" || ext == ".otf" || ext == ".ttc") << file;
  }
  EXPECT_TRUE(pick_default_font(dirs).has_value());
}

TEST(StbFont, ReportsMetricsForSystemFont) {
  const std::shared_ptr<StbFont> font = load_default_font();
  if (font == nullptr) {
    GTEST_SKIP() << "系统里找不到可用字体";
  }
  EXPECT_GT(font->line_height(16.f), 0.f);
  EXPECT_GT(font->ascent(16.f), 0.f);
  EXPECT_LT(font->ascent(16.f), font->line_height(16.f) + 1.f);
  EXPECT_TRUE(font->has_glyph(U'A'));
  EXPECT_TRUE(font->has_glyph(U'a'));
  const GlyphMetrics glyph = font->metrics(U'H', 16.f);
  EXPECT_TRUE(glyph.valid);
  EXPECT_GT(glyph.advance, 0.f);
  EXPECT_GT(glyph.height, 0.f);
  EXPECT_GT(glyph.bearing_y, 0.f);  // 位图顶在基线之上
}

TEST(StbFont, RasterizesVisibleGlyphs) {
  const std::shared_ptr<StbFont> font = load_default_font();
  if (font == nullptr) {
    GTEST_SKIP() << "系统里找不到可用字体";
  }
  const GlyphBitmap bitmap = font->rasterize(U'H', 32.f);
  ASSERT_FALSE(bitmap.empty());
  EXPECT_GT(bitmap.width, 4);
  EXPECT_GT(bitmap.height, 8);
  EXPECT_EQ(bitmap.alpha.size(), bitmap.pixel_count());
  EXPECT_TRUE(std::any_of(bitmap.alpha.begin(), bitmap.alpha.end(),
                          [](std::uint8_t alpha) { return alpha > 0; }));
  EXPECT_TRUE(font->rasterize(U' ', 32.f).empty());  // 空格没有墨迹
}

TEST(StbFont, MetricsScaleWithPixelSize) {
  const std::shared_ptr<StbFont> font = load_default_font();
  if (font == nullptr) {
    GTEST_SKIP() << "系统里找不到可用字体";
  }
  const GlyphMetrics small = font->metrics(U'M', 16.f);
  const GlyphMetrics large = font->metrics(U'M', 32.f);
  EXPECT_NEAR(large.advance, small.advance * 2.f, 0.6f);
  EXPECT_NEAR(large.height, small.height * 2.f, 2.f);
  EXPECT_NEAR(font->line_height(32.f), font->line_height(16.f) * 2.f, 1.f);
}

TEST(StbFont, RejectsInvalidFontData) {
  std::vector<std::uint8_t> garbage(64, 0x5A);
  EXPECT_FALSE(StbFont::from_bytes(std::move(garbage)).has_value());
  EXPECT_FALSE(StbFont::from_bytes({}).has_value());
}

}  // namespace
}  // namespace tamias
