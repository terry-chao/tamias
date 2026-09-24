#include "engine/render/text/text_layout.h"

#include "engine/render/text/text_utf8.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace tamias {
namespace {

// 行尾空白不画也不占宽；这里把「能在哪里断」也一次说清。
bool is_space(char32_t c) {
  return c == U' ' || c == U'\t' || c == U'\r' || c == U'\n' || c == U'\u00A0' ||
         c == U'\u3000';
}

// 中日韩 / 全角：字与字之间可以断行（中文没有词边界）。
bool is_cjk(char32_t c) {
  return (c >= 0x2E80u && c <= 0x9FFFu) || (c >= 0xF900u && c <= 0xFAFFu) ||
         (c >= 0xFE30u && c <= 0xFE4Fu) || (c >= 0xFF00u && c <= 0xFFEFu) ||
         (c >= 0x20000u && c <= 0x2FA1Fu);
}

// 码点流里的一段连续区间。
struct Range {
  std::size_t begin = 0;
  std::size_t end = 0;
};

class ParagraphLayouter {
 public:
  ParagraphLayouter(const std::u32string& chars, const TextStyle& style,
                    const GlyphMetricsProvider& metrics, float max_width)
      : chars_(chars), style_(style), metrics_(metrics), max_width_(max_width) {}

  // 把一个段落（不含 '\n'）排成若干行，追加进 out。
  void run(std::size_t begin, std::size_t end, std::vector<TextLine>& out) const {
    std::vector<Range> atoms = split_atoms(begin, end);
    std::vector<Range> line;
    float line_width = 0.f;
    for (const Range& atom : atoms) {
      const float atom_width = width_of(atom);
      const bool atom_is_space = is_space(chars_[atom.begin]);
      if (max_width_ > 0.f && !line.empty() && line_width + atom_width > kEpsilon + max_width_) {
        flush(out, line);
        line.clear();
        line_width = 0.f;
        // 断行处的空白吃掉，不要顶到下一行行首。
        if (atom_is_space) {
          continue;
        }
      }
      line.push_back(atom);
      line_width += atom_width;
    }
    flush(out, line);
  }

  [[nodiscard]] float width_of(const Range& range) const {
    float width = 0.f;
    for (std::size_t i = range.begin; i < range.end; ++i) {
      width += metrics_.metrics(chars_[i], style_.size).advance;
      if (i + 1 < range.end) {
        width += metrics_.kerning(chars_[i], chars_[i + 1], style_.size);
      }
    }
    width += style_.letter_spacing * static_cast<float>(range.end - range.begin);
    return width;
  }

 private:
  static constexpr float kEpsilon = 1e-4f;

  // 断行单位：一段词、一段空白、或一个 CJK 字。长过一行的词在这里先切碎，
  // 保证后续贪心放得下。
  [[nodiscard]] std::vector<Range> split_atoms(std::size_t begin, std::size_t end) const {
    std::vector<Range> units;
    std::size_t i = begin;
    while (i < end) {
      const char32_t c = chars_[i];
      if (is_cjk(c)) {
        units.push_back(Range{i, i + 1});
        ++i;
        continue;
      }
      const bool space = is_space(c);
      std::size_t j = i;
      while (j < end && !is_cjk(chars_[j]) && is_space(chars_[j]) == space) {
        ++j;
      }
      units.push_back(Range{i, j});
      i = j;
    }

    if (max_width_ <= 0.f) {
      return units;
    }
    std::vector<Range> atoms;
    for (const Range& unit : units) {
      if (is_space(chars_[unit.begin]) || width_of(unit) <= max_width_ + kEpsilon) {
        atoms.push_back(unit);
        continue;
      }
      // 硬断：一个词放不下一行，按字切成放得下的块。
      std::size_t start = unit.begin;
      while (start < unit.end) {
        std::size_t stop = start;
        float width = 0.f;
        while (stop < unit.end) {
          float step = metrics_.metrics(chars_[stop], style_.size).advance + style_.letter_spacing;
          if (stop + 1 < unit.end) {
            step += metrics_.kerning(chars_[stop], chars_[stop + 1], style_.size);
          }
          if (stop > start && width + step > max_width_ + kEpsilon) {
            break;
          }
          width += step;
          ++stop;
        }
        if (stop == start) {
          ++stop;  // 一个字就超宽：一个字独占一行，总得往前走
        }
        atoms.push_back(Range{start, stop});
        start = stop;
      }
    }
    return atoms;
  }

  // 行尾空白去掉后再落一行。
  void flush(std::vector<TextLine>& out, const std::vector<Range>& line) const {
    if (line.empty()) {
      return;
    }
    std::size_t line_begin = line.front().begin;
    std::size_t line_end = line.back().end;
    while (line_end > line_begin && is_space(chars_[line_end - 1])) {
      --line_end;
    }
    const float trimmed = width_of(Range{line_begin, line_end});
    out.push_back(TextLine{});
    out.back().begin = line_begin;
    out.back().end = line_end;
    out.back().width = trimmed;
  }

  const std::u32string& chars_;
  const TextStyle& style_;
  const GlyphMetricsProvider& metrics_;
  float max_width_ = 0.f;
};

}  // namespace

TextLayout layout_text(std::string_view utf8, const TextStyle& style, TextAlign align,
                       float max_width, const GlyphMetricsProvider& metrics) {
  TextLayout layout;
  const std::u32string chars = decode_utf8(utf8);
  if (chars.empty()) {
    return layout;
  }

  const float font_line_height = metrics.line_height(style.size);
  const float ascent = metrics.ascent(style.size);
  const float spacing = style.line_spacing > 0.f ? style.line_spacing : 1.f;
  const float line_height = font_line_height * spacing;

  // 段落 = '\n' 之间；空段落（连续换行）也要占一行高，否则行距会塌。
  ParagraphLayouter paragraph(chars, style, metrics, max_width);
  std::vector<TextLine> raw_lines;
  std::size_t paragraph_begin = 0;
  for (std::size_t i = 0; i <= chars.size(); ++i) {
    const bool at_end = i == chars.size();
    if (!at_end && chars[i] != U'\n') {
      continue;
    }
    std::size_t before = raw_lines.size();
    paragraph.run(paragraph_begin, i, raw_lines);
    if (raw_lines.size() == before) {
      raw_lines.push_back(TextLine{});  // 空段落
      raw_lines.back().begin = paragraph_begin;
      raw_lines.back().end = paragraph_begin;
    }
    paragraph_begin = i + 1;
  }

  layout.lines = std::move(raw_lines);
  layout.line_height = line_height;
  layout.first_baseline = ascent;
  layout.height = line_height * static_cast<float>(layout.lines.size());

  for (const TextLine& line : layout.lines) {
    layout.width = (std::max)(layout.width, line.width);
  }
  const float reference = max_width > 0.f ? max_width : layout.width;

  // 第二遍：对齐全行 + 放笔位（第一遍只量宽，不知道参考宽度）。
  for (std::size_t index = 0; index < layout.lines.size(); ++index) {
    TextLine& line = layout.lines[index];
    float x = 0.f;
    switch (align) {
      case TextAlign::Center:
        x = (reference - line.width) * 0.5f;
        break;
      case TextAlign::Right:
        x = reference - line.width;
        break;
      case TextAlign::Left:
        break;
    }
    line.baseline = ascent + line_height * static_cast<float>(index);
    line.glyphs.reserve(line.end - line.begin);
    float pen = x;
    for (std::size_t i = line.begin; i < line.end; ++i) {
      const GlyphMetrics glyph = metrics.metrics(chars[i], style.size);
      PositionedGlyph positioned{};
      positioned.codepoint = chars[i];
      positioned.x = pen;
      positioned.y = line.baseline;
      positioned.metrics = glyph;
      line.glyphs.push_back(positioned);
      pen += glyph.advance + style.letter_spacing;
      if (i + 1 < line.end) {
        pen += metrics.kerning(chars[i], chars[i + 1], style.size);
      }
    }
    layout.glyph_count += line.glyphs.size();
  }
  return layout;
}

}  // namespace tamias
