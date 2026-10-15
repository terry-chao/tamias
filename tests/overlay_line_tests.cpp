#include "engine/render/runtime/line_instance.h"

#include <gtest/gtest.h>

namespace tamias {
namespace {

// 实例布局是 shader 的契约（line.vert.hlsl 按这几个字段读）：起点进 row0.xyz、
// 线宽进 row0.w、终点进 row1.xyz、颜色不预乘。改这里就得同步改那边。
TEST(OverlayLine, InstanceLayoutMatchesShaderContract) {
  OverlayLine line{};
  line.start = {1.f, 2.f, 3.f};
  line.end = {-4.f, 5.f, -6.f};
  line.color = {0.25f, 0.5f, 0.75f};
  line.width_px = 2.5f;

  const GpuInstance inst = make_line_instance(line);

  EXPECT_FLOAT_EQ(inst.row0[0], 1.f);
  EXPECT_FLOAT_EQ(inst.row0[1], 2.f);
  EXPECT_FLOAT_EQ(inst.row0[2], 3.f);
  EXPECT_FLOAT_EQ(inst.row0[3], 2.5f) << "第四列是线宽（像素）";
  EXPECT_FLOAT_EQ(inst.row1[0], -4.f);
  EXPECT_FLOAT_EQ(inst.row1[1], 5.f);
  EXPECT_FLOAT_EQ(inst.row1[2], -6.f);
  EXPECT_FLOAT_EQ(inst.color[0], 0.25f);
  EXPECT_FLOAT_EQ(inst.color[1], 0.5f);
  EXPECT_FLOAT_EQ(inst.color[2], 0.75f);
  EXPECT_FLOAT_EQ(inst.color[3], 1.f) << "颜色不预乘：alpha 由片元乘上去";
}

// GpuInstance 是四条后端共用的定长记录；线条实例复用它，所以大小不能擅自改。
TEST(OverlayLine, InstanceRecordStaysGpuInstanceSized) {
  EXPECT_EQ(sizeof(GpuInstance), 96u);
}

}  // namespace
}  // namespace tamias
