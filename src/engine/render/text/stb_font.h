#pragma once

#include "engine/base/result.h"
#include "engine/render/text/glyph_metrics_provider.h"
#include "engine/render/text/glyph_rasterizer.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tamias {

// 用 vendored `stb_truetype` 解析字体文件：度量（布局用）+ 光栅化（图集用）。
// 一个对象同时是 GlyphMetricsProvider 和 GlyphRasterizer——同一个字体要保证两边
// 用的是同一份度量，拆成两个对象迟早对不上。
//
// 支持 .ttf / .otf / .ttc（face_index 选集合里的第几套字，.ttf 恒为 0）。
//
// ⚠️ stb_truetype 对字体文件不做越界检查：只喂**受信任的来源**
//（仓库 assets/fonts 或系统字体目录），别拿用户随手塞的文件当输入。
class StbFont final : public GlyphMetricsProvider, public GlyphRasterizer {
 public:
  static Result<std::shared_ptr<StbFont>> load_file(const std::filesystem::path& path,
                                                    int face_index = 0);
  static Result<std::shared_ptr<StbFont>> from_bytes(std::vector<std::uint8_t> bytes,
                                                     int face_index = 0);
  ~StbFont() override;

  [[nodiscard]] GlyphMetrics metrics(std::uint32_t codepoint, float px_size) const override;
  [[nodiscard]] float line_height(float px_size) const override;
  [[nodiscard]] float ascent(float px_size) const override;
  [[nodiscard]] float kerning(std::uint32_t left, std::uint32_t right,
                              float px_size) const override;

  [[nodiscard]] GlyphBitmap rasterize(std::uint32_t codepoint, float px_size) const override;
  [[nodiscard]] bool has_glyph(std::uint32_t codepoint) const override;

  // 稳定的字体序号（GlyphKey::font_id 用）；同一次进程里不会重复。
  [[nodiscard]] std::uint32_t id() const;
  // 字体自报的家族名（"Segoe UI" / "Microsoft YaHei"）；读不到就是空串。
  [[nodiscard]] const std::string& family() const;

 private:
  struct Impl;
  explicit StbFont(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace tamias
