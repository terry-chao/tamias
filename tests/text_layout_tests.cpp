#include "engine/render/text/text_item.h"
#include "engine/render/text/text_layout.h"
#include "engine/render/text/text_utf8.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace tamias {
namespace {

// 假字体：拉丁 0.6 em、空格 0.5 em、CJK 1.0 em；行高 1.0 em、ascent 0.8 em。
// size = 10 时：拉丁 6、空格 5、CJK 10、行高 10、基线 8。
// 度量写成假的是故意的——布局的正确性不该依赖某个具体字体文件。
class FakeMetrics final : public GlyphMetricsProvider {
 public:
  [[nodiscard]] GlyphMetrics metrics(std::uint32_t codepoint, float px_size) const override {
    GlyphMetrics glyph{};
    glyph.valid = true;
    glyph.whitespace = codepoint == U' ';
    const float em = glyph.whitespace ? 0.5f : is_cjk(codepoint) ? 1.0f : 0.6f;
    glyph.advance = em * px_size;
    glyph.width = glyph.advance;
    glyph.height = 0.7f * px_size;
    glyph.bearing_y = 0.7f * px_size;
    return glyph;
  }
  [[nodiscard]] float line_height(float px_size) const override { return px_size; }
  [[nodiscard]] float ascent(float px_size) const override { return 0.8f * px_size; }
  [[nodiscard]] float kerning(std::uint32_t left, std::uint32_t right,
                              float px_size) const override {
    return (left == U'A' && right == U'V') ? -0.1f * px_size : 0.f;
  }

 private:
  [[nodiscard]] static bool is_cjk(std::uint32_t c) {
    return c >= 0x2E80u && c <= 0x9FFFu;
  }
};

TextStyle style_10() {
  TextStyle style{};
  style.size = 10.f;
  style.line_spacing = 1.f;
  return style;
}

TextLayout layout(const std::string& text, float max_width = 0.f,
                  TextAlign align = TextAlign::Left, const TextStyle& style = style_10()) {
  static const FakeMetrics metrics;
  return layout_text(text, style, align, max_width, metrics);
}

// ---------------------------------------------------------------- UTF-8

TEST(TextUtf8, DecodesAsciiAndMultibyte) {
  EXPECT_EQ(decode_utf8("AB").size(), 2u);
  const std::u32string han = decode_utf8("中文");
  ASSERT_EQ(han.size(), 2u);
  EXPECT_EQ(han[0], 0x4E2Du);
  EXPECT_EQ(han[1], 0x6587u);
  const std::u32string emoji = decode_utf8("\xF0\x9F\x98\x80");  // U+1F600
  ASSERT_EQ(emoji.size(), 1u);
  EXPECT_EQ(emoji[0], 0x1F600u);
}

TEST(TextUtf8, ReplacesInvalidSequences) {
  EXPECT_EQ(decode_utf8("\x80"), std::u32string(1, 0xFFFD));  // 孤立续字节
  // 截断的三字节序列：结构不完整，只吞首字节，后面的续字节再各出一个替换符。
  EXPECT_EQ(decode_utf8("\xE4\xB8"), std::u32string(2, 0xFFFD));
  // 结构完整但码位非法：整段只换一个替换符。
  EXPECT_EQ(decode_utf8("\xC0\x80"), std::u32string(1, 0xFFFD));      // 过长编码（NUL）
  EXPECT_EQ(decode_utf8("\xED\xA0\x80"), std::u32string(1, 0xFFFD));  // 代理区 U+D800
  EXPECT_EQ(decode_utf8("\xF5\x80\x80\x80"), std::u32string(1, 0xFFFD));  // 超 U+10FFFF
  // 脏字节不该把合法内容一起丢掉。
  const std::u32string mixed = decode_utf8("A\x80" "B");  // \x80B 会被当一个转义
  ASSERT_EQ(mixed.size(), 3u);
  EXPECT_EQ(mixed[0], U'A');
  EXPECT_EQ(mixed[1], 0xFFFD);
  EXPECT_EQ(mixed[2], U'B');
}

// ---------------------------------------------------------------- 基本度量

TEST(TextLayout, EmptyTextHasNoLines) {
  const TextLayout result = layout("");
  EXPECT_TRUE(result.empty());
  EXPECT_EQ(result.width, 0.f);
  EXPECT_EQ(result.height, 0.f);
  EXPECT_EQ(result.glyph_count, 0u);
}

TEST(TextLayout, SingleLineAdvancesAndBaseline) {
  const TextLayout result = layout("AB");
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_FLOAT_EQ(result.lines[0].width, 12.f);
  EXPECT_FLOAT_EQ(result.width, 12.f);
  EXPECT_FLOAT_EQ(result.line_height, 10.f);
  EXPECT_FLOAT_EQ(result.first_baseline, 8.f);
  EXPECT_FLOAT_EQ(result.height, 10.f);
  ASSERT_EQ(result.lines[0].glyphs.size(), 2u);
  EXPECT_FLOAT_EQ(result.lines[0].glyphs[0].x, 0.f);
  EXPECT_FLOAT_EQ(result.lines[0].glyphs[1].x, 6.f);
  EXPECT_FLOAT_EQ(result.lines[0].glyphs[0].y, 8.f);
  EXPECT_EQ(result.glyph_count, 2u);
}

TEST(TextLayout, KerningShiftsFollowingGlyph) {
  const TextLayout result = layout("AV");
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_FLOAT_EQ(result.lines[0].glyphs[1].x, 5.f);  // 6 - 1
  EXPECT_FLOAT_EQ(result.lines[0].width, 11.f);
}

TEST(TextLayout, ExplicitNewlineStartsNewLine) {
  const TextLayout result = layout("A\nB");
  ASSERT_EQ(result.lines.size(), 2u);
  EXPECT_FLOAT_EQ(result.lines[0].baseline, 8.f);
  EXPECT_FLOAT_EQ(result.lines[1].baseline, 18.f);
  EXPECT_FLOAT_EQ(result.height, 20.f);
}

TEST(TextLayout, BlankParagraphKeepsItsLine) {
  const TextLayout result = layout("A\n\nB");
  ASSERT_EQ(result.lines.size(), 3u);
  EXPECT_TRUE(result.lines[1].glyphs.empty());
  EXPECT_FLOAT_EQ(result.lines[1].width, 0.f);
  EXPECT_FLOAT_EQ(result.height, 30.f);
  EXPECT_FLOAT_EQ(result.lines[2].baseline, 28.f);
}

// ---------------------------------------------------------------- 折行

TEST(TextLayout, WrapsAtSpaceAndDropsTrailingSpace) {
  const TextLayout result = layout("aaa bbb", 20.f);
  ASSERT_EQ(result.lines.size(), 2u);
  EXPECT_FLOAT_EQ(result.lines[0].width, 18.f);
  EXPECT_FLOAT_EQ(result.lines[1].width, 18.f);
  EXPECT_EQ(result.lines[0].glyphs.size(), 3u);  // 行尾空格不产生字形
  EXPECT_EQ(result.lines[1].begin, 4u);
}

TEST(TextLayout, BreaksBetweenCjkCharacters) {
  const TextLayout result = layout("中文中文", 25.f);
  ASSERT_EQ(result.lines.size(), 2u);
  EXPECT_FLOAT_EQ(result.lines[0].width, 20.f);
  EXPECT_EQ(result.lines[0].glyphs.size(), 2u);
  EXPECT_EQ(result.lines[1].begin, 2u);
}

TEST(TextLayout, HardBreaksWordLongerThanLine) {
  const TextLayout result = layout("aaaaaaaa", 20.f);
  ASSERT_EQ(result.lines.size(), 3u);
  EXPECT_FLOAT_EQ(result.lines[0].width, 18.f);
  EXPECT_FLOAT_EQ(result.lines[1].width, 18.f);
  EXPECT_FLOAT_EQ(result.lines[2].width, 12.f);
}

TEST(TextLayout, NoWrapKeepsSingleLine) {
  const TextLayout result = layout("aaa bbb");
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_FLOAT_EQ(result.lines[0].width, 18.f + 5.f + 18.f);
}

// ---------------------------------------------------------------- 对齐

TEST(TextLayout, AlignsInsideWidth) {
  const TextLayout centered = layout("aa", 40.f, TextAlign::Center);
  ASSERT_EQ(centered.lines.size(), 1u);
  EXPECT_FLOAT_EQ(centered.lines[0].glyphs[0].x, 14.f);  // (40 - 12) / 2
  const TextLayout right = layout("aa", 40.f, TextAlign::Right);
  ASSERT_EQ(right.lines.size(), 1u);
  EXPECT_FLOAT_EQ(right.lines[0].glyphs[0].x, 28.f);  // 40 - 12
  const TextLayout left = layout("aa", 40.f, TextAlign::Left);
  ASSERT_EQ(left.lines.size(), 1u);
  EXPECT_FLOAT_EQ(left.lines[0].glyphs[0].x, 0.f);
}

TEST(TextLayout, AlignmentUsesNaturalWidthWhenNotWrapping) {
  const TextLayout result = layout("aa\nbbbb", 0.f, TextAlign::Center);
  ASSERT_EQ(result.lines.size(), 2u);
  EXPECT_FLOAT_EQ(result.width, 24.f);                // 最长一行
  EXPECT_FLOAT_EQ(result.lines[0].glyphs[0].x, 6.f);  // (24 - 12) / 2
  EXPECT_FLOAT_EQ(result.lines[1].glyphs[0].x, 0.f);
}

// ---------------------------------------------------------------- 样式

TEST(TextLayout, LineSpacingScalesLineHeight) {
  TextStyle style = style_10();
  style.line_spacing = 1.5f;
  const TextLayout result = layout("A\nB", 0.f, TextAlign::Left, style);
  ASSERT_EQ(result.lines.size(), 2u);
  EXPECT_FLOAT_EQ(result.line_height, 15.f);
  EXPECT_FLOAT_EQ(result.lines[1].baseline, 23.f);  // 8 + 15
  EXPECT_FLOAT_EQ(result.height, 30.f);
}

TEST(TextLayout, LetterSpacingWidensText) {
  TextStyle style = style_10();
  style.letter_spacing = 2.f;
  const TextLayout result = layout("AB", 0.f, TextAlign::Left, style);
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_FLOAT_EQ(result.lines[0].glyphs[1].x, 8.f);  // 6 + 2
  EXPECT_FLOAT_EQ(result.lines[0].width, 16.f);
}

TEST(TextLayout, SpaceOnlyTextIsOneEmptyLine) {
  const TextLayout result = layout(" ");
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_TRUE(result.lines[0].glyphs.empty());
  EXPECT_FLOAT_EQ(result.width, 0.f);
  EXPECT_FLOAT_EQ(result.height, 10.f);
}

}  // namespace
}  // namespace tamias
