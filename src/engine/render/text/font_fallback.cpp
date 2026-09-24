#include "engine/render/text/font_fallback.h"

#include "engine/render/text/text_utf8.h"

#include <cstdint>
#include <string>

namespace tamias {

const StbFont* pick_font_for_text(std::string_view utf8,
                                  const std::vector<std::shared_ptr<StbFont>>& fonts) {
  if (utf8.empty() || fonts.empty()) {
    return nullptr;
  }
  const std::u32string codepoints = decode_utf8(utf8);
  for (const std::shared_ptr<StbFont>& font : fonts) {
    if (font == nullptr) {
      continue;
    }
    bool covers_all = true;
    for (char32_t codepoint : codepoints) {
      if (codepoint == U'\n' || codepoint == U' ' || codepoint == U'\t') {
        continue;  // 空白不需要字形
      }
      if (!font->has_glyph(static_cast<std::uint32_t>(codepoint))) {
        covers_all = false;
        break;
      }
    }
    if (covers_all) {
      return font.get();
    }
  }
  return nullptr;
}

}  // namespace tamias
