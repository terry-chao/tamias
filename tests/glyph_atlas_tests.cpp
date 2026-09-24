#include "engine/render/text/glyph_atlas.h"
#include "engine/render/text/glyph_rasterizer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace tamias {
namespace {

// 假字体：每个字都是一块 size × size 的实心方块，空格没有墨迹。
// 不碰字体文件——图集的装箱 / 缓存 / 逐出该被单独测。
class BoxRasterizer final : public GlyphRasterizer {
 public:
  mutable int rasterize_calls = 0;

  [[nodiscard]] GlyphBitmap rasterize(std::uint32_t codepoint, float px_size) const override {
    ++rasterize_calls;
    GlyphBitmap bitmap{};
    if (codepoint == U' ' || px_size <= 0.f) {
      return bitmap;
    }
    const int size = static_cast<int>(px_size);
    bitmap.width = size;
    bitmap.height = size;
    bitmap.bearing_x = 1.f;
    bitmap.bearing_y = static_cast<float>(size);
    bitmap.alpha.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 200);
    return bitmap;
  }
};

GlyphKey glyph_key(std::uint32_t codepoint, float px_size = 16.f) {
  return GlyphKey{1, codepoint, quantize_glyph_px(px_size)};
}

bool overlaps(const GlyphSlot& a, const GlyphSlot& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
         b.y < a.y + a.height;
}

TEST(GlyphAtlas, CachesByKey) {
  GlyphAtlas atlas(64, 64, 0);
  BoxRasterizer raster;
  const GlyphSlot* first = atlas.acquire(glyph_key(U'A'), raster, 1);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(raster.rasterize_calls, 1);
  EXPECT_EQ(atlas.glyph_count(), 1u);

  const GlyphSlot* second = atlas.acquire(glyph_key(U'A'), raster, 2);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(raster.rasterize_calls, 1);  // 命中缓存，没有再光栅化
  EXPECT_EQ(second->x, first->x);
  EXPECT_EQ(second->y, first->y);
  EXPECT_EQ(atlas.glyph_count(), 1u);
}

TEST(GlyphAtlas, PacksGlyphsWithoutOverlap) {
  GlyphAtlas atlas(64, 64, 0);
  BoxRasterizer raster;
  std::vector<GlyphSlot> slots;
  for (std::uint32_t codepoint : {U'A', U'B', U'C', U'D'}) {
    const GlyphSlot* slot = atlas.acquire(glyph_key(codepoint), raster, 1);
    ASSERT_NE(slot, nullptr);
    slots.push_back(*slot);
  }
  EXPECT_EQ(atlas.glyph_count(), 4u);
  for (std::size_t i = 0; i < slots.size(); ++i) {
    EXPECT_GE(slots[i].u0, 0.f);
    EXPECT_LE(slots[i].u1, 1.f);
    EXPECT_GE(slots[i].v0, 0.f);
    EXPECT_LE(slots[i].v1, 1.f);
    for (std::size_t j = i + 1; j < slots.size(); ++j) {
      EXPECT_FALSE(overlaps(slots[i], slots[j])) << "字形 " << i << " 和 " << j << " 重叠";
    }
  }
}

TEST(GlyphAtlas, WritesPremultipliedWhitePixels) {
  GlyphAtlas atlas(64, 64, 0);
  BoxRasterizer raster;
  const GlyphSlot* slot = atlas.acquire(glyph_key(U'A'), raster, 1);
  ASSERT_NE(slot, nullptr);
  const std::size_t offset =
      (static_cast<std::size_t>(slot->y) * 64u + static_cast<std::size_t>(slot->x)) * 4u;
  const std::vector<std::uint8_t>& pixels = atlas.rgba();
  ASSERT_GT(pixels.size(), offset + 3);
  EXPECT_EQ(pixels[offset + 0], 200);
  EXPECT_EQ(pixels[offset + 1], 200);
  EXPECT_EQ(pixels[offset + 2], 200);
  EXPECT_EQ(pixels[offset + 3], 200);  // 预乘：rgb == a
}

TEST(GlyphAtlas, SpaceDoesNotOccupyAtlas) {
  GlyphAtlas atlas(64, 64, 0);
  BoxRasterizer raster;
  EXPECT_EQ(atlas.acquire(glyph_key(U' '), raster, 1), nullptr);
  EXPECT_EQ(atlas.glyph_count(), 0u);
}

TEST(GlyphAtlas, EvictsLeastRecentlyUsedRowWhenFull) {
  GlyphAtlas atlas(32, 32, 0);  // 刚好放 2 × 2 个 16 px 方块
  BoxRasterizer raster;
  ASSERT_NE(atlas.acquire(glyph_key(U'A'), raster, 1), nullptr);
  ASSERT_NE(atlas.acquire(glyph_key(U'B'), raster, 2), nullptr);
  ASSERT_NE(atlas.acquire(glyph_key(U'C'), raster, 3), nullptr);
  ASSERT_NE(atlas.acquire(glyph_key(U'D'), raster, 4), nullptr);
  EXPECT_EQ(atlas.glyph_count(), 4u);

  // 装不下：A/B 那行最久没用过（last_use = 2），整行作废腾位置。
  ASSERT_NE(atlas.acquire(glyph_key(U'E'), raster, 5), nullptr);
  EXPECT_EQ(atlas.find(glyph_key(U'A')), nullptr);
  EXPECT_EQ(atlas.find(glyph_key(U'B')), nullptr);
  EXPECT_NE(atlas.find(glyph_key(U'C')), nullptr);
  EXPECT_NE(atlas.find(glyph_key(U'D')), nullptr);
  EXPECT_EQ(atlas.glyph_count(), 3u);
}

TEST(GlyphAtlas, GrowsBeforeEvicting) {
  GlyphAtlas atlas(32, 128, 0);
  BoxRasterizer raster;
  const GlyphSlot* first = atlas.acquire(glyph_key(U'A'), raster, 1);
  ASSERT_NE(first, nullptr);
  const GlyphSlot kept = *first;  // 存值：扩容会重算 UV
  for (std::uint32_t codepoint : {U'B', U'C', U'D'}) {
    ASSERT_NE(atlas.acquire(glyph_key(codepoint), raster, 2), nullptr);
  }
  EXPECT_EQ(atlas.size(), 32);

  ASSERT_NE(atlas.acquire(glyph_key(U'E'), raster, 3), nullptr);
  EXPECT_EQ(atlas.size(), 64);  // 先扩容，不急着逐出

  const GlyphSlot* still_there = atlas.find(glyph_key(U'A'));
  ASSERT_NE(still_there, nullptr);
  EXPECT_EQ(still_there->x, kept.x);
  EXPECT_EQ(still_there->y, kept.y);
  // 像素坐标不变，UV 按新尺寸重算。
  EXPECT_FLOAT_EQ(still_there->u0 * 64.f, static_cast<float>(still_there->x));
  EXPECT_FLOAT_EQ(still_there->u1 * 64.f,
                  static_cast<float>(still_there->x + still_there->width));
  EXPECT_NE(still_there->u1, kept.u1);
}

TEST(GlyphAtlas, GenerationTracksPixelChanges) {
  GlyphAtlas atlas(64, 64, 0);
  BoxRasterizer raster;
  const std::uint64_t before = atlas.generation();
  ASSERT_NE(atlas.acquire(glyph_key(U'A'), raster, 1), nullptr);
  const std::uint64_t after = atlas.generation();
  EXPECT_GT(after, before);
  ASSERT_NE(atlas.acquire(glyph_key(U'A'), raster, 2), nullptr);
  EXPECT_EQ(atlas.generation(), after);  // 缓存命中不动像素
}

TEST(GlyphAtlas, QuantizedSizesShareGlyphs) {
  EXPECT_EQ(quantize_glyph_px(14.1f), 14.f);
  EXPECT_EQ(quantize_glyph_px(13.9f), 14.f);
  GlyphAtlas atlas(64, 64, 0);
  BoxRasterizer raster;
  ASSERT_NE(atlas.acquire(GlyphKey{1, U'A', quantize_glyph_px(14.0f)}, raster, 1), nullptr);
  ASSERT_NE(atlas.acquire(GlyphKey{1, U'A', quantize_glyph_px(14.1f)}, raster, 2), nullptr);
  EXPECT_EQ(atlas.glyph_count(), 1u);
}

TEST(GlyphAtlas, ClearDropsEverything) {
  GlyphAtlas atlas(64, 64, 0);
  BoxRasterizer raster;
  ASSERT_NE(atlas.acquire(glyph_key(U'A'), raster, 1), nullptr);
  const std::uint64_t before = atlas.generation();
  atlas.clear();
  EXPECT_EQ(atlas.glyph_count(), 0u);
  EXPECT_EQ(atlas.find(glyph_key(U'A')), nullptr);
  EXPECT_GT(atlas.generation(), before);
}

}  // namespace
}  // namespace tamias
