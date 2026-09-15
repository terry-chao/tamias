#pragma once

#include "engine/base/result.h"
#include "engine/drawing/drawing.h"
#include "engine/math/aabb2.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace tamias {

// DWF / DWFx 里的一页：图元在合并后的 Drawing 里的区间 + 这一页的范围。
// DWFx 的一页 = 一个 .fpage（XPS 的 FixedPage）。
struct DwfPage {
  Aabb2 bounds{};
  std::size_t path_begin = 0;
  std::size_t path_end = 0;
  std::size_t text_begin = 0;
  std::size_t text_end = 0;
};

// 读 DWF 包的结果。
// - DWFx（Autodesk 用 XPS 包出来的那种）：解析成矢量，`pages` 一页一项。
// - 二进制 DWF（W2D 矢量流）：**还没解**，退回包里自带的预览位图（`preview`）。
//   两者都没有就返回错误。
struct DwfContent {
  Drawing drawing;                // 所有页合在一起，页与页之间用 DwfPage 的区间划分
  std::vector<DwfPage> pages;
  std::vector<std::uint8_t> preview;  // PNG / JPG 原字节（二进制 DWF 的兜底）
  std::size_t skipped_glyphs = 0;     // 没有 UnicodeString、还原不出的文字
  std::size_t skipped_images = 0;     // 图元里的位图刷（还没画）
  bool xps = false;                   // true = DWFx（XPS）

  [[nodiscard]] bool has_vector() const { return !drawing.empty(); }
  [[nodiscard]] bool has_preview() const { return !preview.empty(); }
};

// bytes = 整个 .dwf / .dwfx 文件。两处的坐标统一成**图纸坐标（Y 向上）**：
// XPS 是 Y 向下的，解析时按页高翻过来，画的时候和 DXF 一套逻辑。
[[nodiscard]] Result<DwfContent> read_dwf(std::span<const std::uint8_t> bytes);

// 从文件读（小工具，测试与 app 都用）。
[[nodiscard]] Result<DwfContent> read_dwf_file(const std::filesystem::path& path);

}  // namespace tamias
