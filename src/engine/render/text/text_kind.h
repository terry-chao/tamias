#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace tamias {

// 文字用途。显示开关、拾取策略、默认样式都按它分档（见 docs/TEXT.md §4.4）。
//
// 前 6 类是**派生标注**：内容由 BIM 数据现算，不落盘；
// DrawingText 是**导入文字**：从图纸（DXF/DWF）带进来的。
// 自由注记（Annotation）才是用户创建、进 .tdoc 的实体。
enum class TextKind : std::uint8_t {
  Annotation = 0,   // 自由注记
  AxisLabel = 1,    // 轴网编号（GridAxis::name）
  StoreyLabel = 2,  // 标高 / 楼层名
  Dimension = 3,    // 尺寸链数字
  RoomName = 4,     // 房间名
  FrameTitle = 5,   // 图框标题
  DrawingText = 6,  // 图纸里带进来的文字
};

// 枚举项个数（TextKindSet 的数组长度）。加新 kind 时这里跟着改。
inline constexpr std::size_t kTextKindCount = 7;

[[nodiscard]] inline const char* text_kind_name(TextKind kind) {
  switch (kind) {
    case TextKind::Annotation:
      return "Annotation";
    case TextKind::AxisLabel:
      return "AxisLabel";
    case TextKind::StoreyLabel:
      return "StoreyLabel";
    case TextKind::Dimension:
      return "Dimension";
    case TextKind::RoomName:
      return "RoomName";
    case TextKind::FrameTitle:
      return "FrameTitle";
    case TextKind::DrawingText:
      return "DrawingText";
  }
  return "Annotation";
}

// 反查（命令参数 / 序列化用）。认不出的名字一律当自由注记，不报错——
// 参数脏了不该让整条命令失败。
[[nodiscard]] inline TextKind text_kind_from_name(std::string_view name) {
  if (name == "annotation") return TextKind::Annotation;
  if (name == "axis") return TextKind::AxisLabel;
  if (name == "storey") return TextKind::StoreyLabel;
  if (name == "dimension") return TextKind::Dimension;
  if (name == "room") return TextKind::RoomName;
  if (name == "title") return TextKind::FrameTitle;
  if (name == "drawing") return TextKind::DrawingText;
  return TextKind::Annotation;
}

}  // namespace tamias
