#pragma once

#include "engine/render/text/glyph_key.h"
#include "engine/render/text/glyph_rasterizer.h"
#include "engine/render/text/glyph_slot.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace tamias {

// 字形图集：一行行（shelf）装箱的 RGBA8 位图，一个 draw call 就能画完整屏文字。
//
// 像素格式是**白 + alpha、预乘**（r = g = b = a），和 overlay 管线的
// ONE / ONE_MINUS_SRC_ALPHA 混合对齐，颜色在片元里乘上去。
//
// 三条约定：
//  1) 满了先扩容（到 max_size 为止），再按 **LRU 逐出整行**；逐出会作废该行所有
//     条目（find 变 nullptr），所以消费方**每帧重新 acquire**，别跨帧存指针。
//  2) 空白字形（空格）不占图集，acquire 返回 nullptr，这是正常结果不是错误。
//  3) generation() 变了 = 像素或 UV 变了，消费方据此重传贴图。
class GlyphAtlas {
 public:
  explicit GlyphAtlas(int size = 1024, int max_size = 2048, int padding = 1);

  // 命中直接返回（并刷新 LRU 时间戳）；没命中就光栅化 + 装箱。
  // 放不下时：先扩容，再逐出最久没用过的一行，都不行才返回 nullptr。
  [[nodiscard]] const GlyphSlot* acquire(const GlyphKey& key, const GlyphRasterizer& rasterizer,
                                         std::uint64_t tick);
  // 只查不改 LRU。
  [[nodiscard]] const GlyphSlot* find(const GlyphKey& key) const;

  // 边长翻倍（上限 max_size）。已有字形的像素坐标不变，UV 按新尺寸重算。
  [[nodiscard]] bool grow();

  [[nodiscard]] int size() const { return size_; }
  [[nodiscard]] int max_size() const { return max_size_; }
  [[nodiscard]] std::size_t glyph_count() const { return slots_.size(); }
  [[nodiscard]] std::uint64_t generation() const { return generation_; }
  // size × size × 4 字节，白 + 预乘 alpha。
  [[nodiscard]] const std::vector<std::uint8_t>& rgba() const { return rgba_; }

  void clear();

 private:
  // 一行（shelf）：同行字形高度可以不同，按本行最高的那个算行高。
  struct Row {
    int y = 0;
    int height = 0;
    int used = 0;  // 已用宽度（含 padding）
    std::uint64_t last_use = 0;
    std::vector<GlyphKey> keys;
  };

  [[nodiscard]] int place(int width, int height, std::uint64_t tick);
  [[nodiscard]] bool evict_lru_row();
  void update_uv(GlyphSlot& slot) const;

  int size_ = 1024;
  int max_size_ = 2048;
  int padding_ = 1;
  int shelf_y_ = 0;
  std::uint64_t generation_ = 1;
  std::vector<std::uint8_t> rgba_;
  std::vector<Row> rows_;
  std::vector<Row> free_rows_;  // 逐出留下的空洞（只关心 y / height）
  std::unordered_map<GlyphKey, GlyphSlot, GlyphKeyHash> slots_;
};

}  // namespace tamias
