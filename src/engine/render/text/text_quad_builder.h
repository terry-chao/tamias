#pragma once

#include "engine/math/math.h"
#include "engine/render/runtime/text_quad.h"
#include "engine/render/text/glyph_atlas.h"
#include "engine/render/text/glyph_rasterizer.h"
#include "engine/render/text/text_layout.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tamias {

// 排版结果 → 屏幕四边形。所有屏幕空间文字（轴号 / 标高 / 尺寸 / 标签）都走它。
//
// 三道约定：
//  1) 四边形的像素矩形取**图集槽**（w/h/bearing），不是布局里的度量——保证画的框和
//     真正采样的那张位图严丝合缝；布局的 x/y 只负责笔位。
//  2) 空白字形（图集返回 nullptr，比如空格）直接跳过。
//  3) 颜色不预乘，alpha 在片元里乘。
//
// origin 是文字块左上角在视口里的**设备像素**位置（左上原点）。返回新增的四边形数。
[[nodiscard]] std::size_t append_text_quads(const TextLayout& layout, std::uint32_t font_id,
                                            float origin_x, float origin_y, float px_size,
                                            Vec3 color, float opacity,
                                            const GlyphRasterizer& rasterizer,
                                            GlyphAtlas& atlas, std::uint64_t tick,
                                            std::vector<TextQuad>& out);

}  // namespace tamias
