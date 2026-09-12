#pragma once

#include "engine/core/result.h"
#include "engine/drawing/drawing.h"

#include <filesystem>
#include <string_view>

namespace tamias {

// 读 ASCII DXF（R12 起的组码格式）。二进制 DXF 与 DWG 会被明确拒绝。
// 支持 LINE / CIRCLE / ARC / LWPOLYLINE / POLYLINE / ELLIPSE / TEXT / MTEXT /
// INSERT（含嵌套块）；其余实体计入 unsupported_entity_count()。
[[nodiscard]] Result<Drawing> load_dxf(const std::filesystem::path& path);

// 同一解析器，输入直接给文本（测试用）。
[[nodiscard]] Result<Drawing> parse_dxf(std::string_view text);

}  // namespace tamias
