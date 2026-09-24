#include "engine/render/text/text_quad_builder.h"

#include <cstddef>

namespace tamias {

std::size_t append_text_quads(const TextLayout& layout, std::uint32_t font_id, float origin_x,
                              float origin_y, float px_size, Vec3 color, float opacity,
                              const GlyphRasterizer& rasterizer, GlyphAtlas& atlas,
                              std::uint64_t tick, std::vector<TextQuad>& out) {
  std::size_t added = 0;
  for (const TextLine& line : layout.lines) {
    for (const PositionedGlyph& glyph : line.glyphs) {
      if (glyph.metrics.whitespace) {
        continue;
      }
      GlyphKey key{};
      key.font_id = font_id;
      key.codepoint = glyph.codepoint;
      key.px_size = quantize_glyph_px(px_size);
      const GlyphSlot* slot = atlas.acquire(key, rasterizer, tick);
      if (slot == nullptr) {
        continue;  // 空白或装不下：这一格不画，其余照画
      }
      TextQuad quad{};
      quad.x = origin_x + glyph.x + slot->bearing_x;
      quad.y = origin_y + glyph.y - slot->bearing_y;
      quad.width = static_cast<float>(slot->width);
      quad.height = static_cast<float>(slot->height);
      quad.u0 = slot->u0;
      quad.v0 = slot->v0;
      quad.u1 = slot->u1;
      quad.v1 = slot->v1;
      quad.color[0] = color.x;
      quad.color[1] = color.y;
      quad.color[2] = color.z;
      quad.color[3] = opacity;
      out.push_back(quad);
      ++added;
    }
  }
  return added;
}

}  // namespace tamias
