#include "engine/render/text/glyph_atlas.h"

#include <algorithm>
#include <cstring>

namespace tamias {
namespace {

constexpr std::size_t kBytesPerPixel = 4;

}  // namespace

GlyphAtlas::GlyphAtlas(int size, int max_size, int padding)
    : size_(std::clamp(size, 16, 8192)),
      max_size_(std::clamp(max_size, size_, 8192)),
      padding_(std::max(padding, 0)) {
  rgba_.assign(static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_) * kBytesPerPixel,
               0);
}

const GlyphSlot* GlyphAtlas::acquire(const GlyphKey& key, const GlyphRasterizer& rasterizer,
                                     std::uint64_t tick) {
  auto found = slots_.find(key);
  if (found != slots_.end()) {
    for (Row& row : rows_) {
      if (row.y == found->second.y) {
        row.last_use = tick;
        break;
      }
    }
    return &found->second;
  }

  const GlyphBitmap bitmap = rasterizer.rasterize(key.codepoint, key.px_size);
  if (bitmap.empty()) {
    return nullptr;  // 空白字形：不占图集
  }
  if (bitmap.width > size_ || bitmap.height > size_) {
    return nullptr;  // 单个字形比整张图集还大，装不下
  }

  int row_index = place(bitmap.width, bitmap.height, tick);
  if (row_index < 0 && size_ < max_size_) {
    if (grow()) {
      row_index = place(bitmap.width, bitmap.height, tick);
    }
  }
  if (row_index < 0) {
    if (evict_lru_row()) {
      row_index = place(bitmap.width, bitmap.height, tick);
    }
  }
  if (row_index < 0) {
    return nullptr;
  }

  Row& row = rows_[static_cast<std::size_t>(row_index)];
  const int x = row.used;
  const int y = row.y;
  for (int gy = 0; gy < bitmap.height; ++gy) {
    const std::uint8_t* src = bitmap.alpha.data() + static_cast<std::size_t>(gy) * bitmap.width;
    std::uint8_t* dst =
        rgba_.data() + (static_cast<std::size_t>(y + gy) * size_ + x) * kBytesPerPixel;
    for (int gx = 0; gx < bitmap.width; ++gx) {
      const std::uint8_t alpha = src[gx];
      // 预乘：rgb = alpha（白字），a = alpha。
      dst[0] = alpha;
      dst[1] = alpha;
      dst[2] = alpha;
      dst[3] = alpha;
      dst += kBytesPerPixel;
    }
  }
  row.used += bitmap.width + padding_;
  row.keys.push_back(key);

  GlyphSlot slot{};
  slot.x = x;
  slot.y = y;
  slot.width = bitmap.width;
  slot.height = bitmap.height;
  slot.bearing_x = bitmap.bearing_x;
  slot.bearing_y = bitmap.bearing_y;
  update_uv(slot);
  ++generation_;
  auto inserted = slots_.emplace(key, slot);
  return &inserted.first->second;
}

const GlyphSlot* GlyphAtlas::find(const GlyphKey& key) const {
  auto found = slots_.find(key);
  return found == slots_.end() ? nullptr : &found->second;
}

bool GlyphAtlas::grow() {
  if (size_ >= max_size_) {
    return false;
  }
  const int next = (std::min)(size_ * 2, max_size_);
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(next) * static_cast<std::size_t>(next) * kBytesPerPixel, 0);
  // 已装箱的内容坐标不变，逐行搬到新缓冲（行都在 shelf_y_ 之上）。
  for (int y = 0; y < shelf_y_; ++y) {
    std::memcpy(pixels.data() + static_cast<std::size_t>(y) * next * kBytesPerPixel,
                rgba_.data() + static_cast<std::size_t>(y) * size_ * kBytesPerPixel,
                static_cast<std::size_t>(size_) * kBytesPerPixel);
  }
  rgba_ = std::move(pixels);
  size_ = next;
  for (auto& entry : slots_) {
    update_uv(entry.second);
  }
  ++generation_;
  return true;
}

void GlyphAtlas::clear() {
  std::fill(rgba_.begin(), rgba_.end(), std::uint8_t{0});
  slots_.clear();
  rows_.clear();
  free_rows_.clear();
  shelf_y_ = 0;
  ++generation_;
}

int GlyphAtlas::place(int width, int height, std::uint64_t tick) {
  if (width <= 0 || height <= 0) {
    return -1;
  }
  // ① 已装箱的行里找第一个放得下的。
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    Row& row = rows_[i];
    if (row.height >= height && row.used + width + padding_ <= size_) {
      row.last_use = tick;
      return static_cast<int>(i);
    }
  }
  // ② 逐出留下的空洞。
  for (std::size_t i = 0; i < free_rows_.size(); ++i) {
    if (free_rows_[i].height >= height) {
      Row row{};
      row.y = free_rows_[i].y;
      row.height = free_rows_[i].height;
      row.last_use = tick;
      free_rows_.erase(free_rows_.begin() + static_cast<std::ptrdiff_t>(i));
      rows_.push_back(std::move(row));
      return static_cast<int>(rows_.size()) - 1;
    }
  }
  // ③ 顶部新开一行。
  if (shelf_y_ + height + padding_ <= size_) {
    Row row{};
    row.y = shelf_y_;
    row.height = height;
    row.last_use = tick;
    shelf_y_ += height + padding_;
    rows_.push_back(std::move(row));
    return static_cast<int>(rows_.size()) - 1;
  }
  return -1;
}

bool GlyphAtlas::evict_lru_row() {
  if (rows_.empty()) {
    return false;
  }
  std::size_t victim = 0;
  for (std::size_t i = 1; i < rows_.size(); ++i) {
    if (rows_[i].last_use < rows_[victim].last_use) {
      victim = i;
    }
  }
  Row& row = rows_[victim];
  for (const GlyphKey& key : row.keys) {
    slots_.erase(key);
  }
  const std::size_t row_pixels =
      static_cast<std::size_t>(row.height) * static_cast<std::size_t>(size_);
  std::fill_n(rgba_.begin() + static_cast<std::ptrdiff_t>(
                                 static_cast<std::size_t>(row.y) * size_ * kBytesPerPixel),
              row_pixels * kBytesPerPixel, std::uint8_t{0});
  Row hole{};
  hole.y = row.y;
  hole.height = row.height;
  free_rows_.push_back(hole);
  rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(victim));
  ++generation_;
  return true;
}

void GlyphAtlas::update_uv(GlyphSlot& slot) const {
  const float size = static_cast<float>(size_);
  slot.u0 = static_cast<float>(slot.x) / size;
  slot.v0 = static_cast<float>(slot.y) / size;
  slot.u1 = static_cast<float>(slot.x + slot.width) / size;
  slot.v1 = static_cast<float>(slot.y + slot.height) / size;
}

}  // namespace tamias
