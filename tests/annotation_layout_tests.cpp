#include "engine/render/text/label_occluder.h"
#include "engine/render/text/text_kind_set.h"

#include <gtest/gtest.h>

namespace tamias {
namespace {

TEST(LabelOccluder, ReservesFreeRectsAndRejectsOverlaps) {
  LabelOccluder occluder(0.f);
  EXPECT_TRUE(occluder.try_reserve(0.f, 0.f, 10.f, 10.f));
  EXPECT_TRUE(occluder.try_reserve(20.f, 0.f, 10.f, 10.f));
  EXPECT_EQ(occluder.count(), 2u);
  // 和第一块相交。
  EXPECT_FALSE(occluder.try_reserve(5.f, 5.f, 10.f, 10.f));
  // 贴在边上（不算相交：区间是半开的）。
  EXPECT_TRUE(occluder.try_reserve(10.f, 0.f, 10.f, 10.f));
  EXPECT_EQ(occluder.count(), 3u);
}

TEST(LabelOccluder, PaddingKeepsLabelsApart) {
  LabelOccluder tight(0.f);
  EXPECT_TRUE(tight.try_reserve(0.f, 0.f, 10.f, 10.f));
  EXPECT_TRUE(tight.try_reserve(11.f, 0.f, 10.f, 10.f));  // 1 px 缝：贴着但不相交

  LabelOccluder padded(2.f);
  EXPECT_TRUE(padded.try_reserve(0.f, 0.f, 10.f, 10.f));
  EXPECT_FALSE(padded.try_reserve(11.f, 0.f, 10.f, 10.f));  // padding 把 1 px 缝吃掉了
  // 第一块含 padding 占到 12，所以至少要从 14 起才算真的分开。
  EXPECT_FALSE(padded.try_reserve(13.f, 0.f, 10.f, 10.f));
  EXPECT_TRUE(padded.try_reserve(14.f, 0.f, 10.f, 10.f));
}

TEST(LabelOccluder, IgnoresEmptyRectsAndResets) {
  LabelOccluder occluder;
  EXPECT_FALSE(occluder.try_reserve(0.f, 0.f, 0.f, 10.f));
  EXPECT_FALSE(occluder.try_reserve(0.f, 0.f, 10.f, -1.f));
  EXPECT_EQ(occluder.count(), 0u);

  EXPECT_TRUE(occluder.try_reserve(0.f, 0.f, 10.f, 10.f));
  occluder.reset();
  EXPECT_EQ(occluder.count(), 0u);
  EXPECT_TRUE(occluder.try_reserve(0.f, 0.f, 10.f, 10.f));  // 清空后又能占同一块
}

TEST(TextKindSet, DefaultsToEverythingVisible) {
  const TextKindSet kinds;
  EXPECT_TRUE(kinds.visible(TextKind::Annotation));
  EXPECT_TRUE(kinds.visible(TextKind::AxisLabel));
  EXPECT_TRUE(kinds.visible(TextKind::StoreyLabel));
  EXPECT_TRUE(kinds.visible(TextKind::Dimension));
  EXPECT_TRUE(kinds.visible(TextKind::RoomName));
  EXPECT_TRUE(kinds.visible(TextKind::FrameTitle));
  EXPECT_TRUE(kinds.visible(TextKind::DrawingText));
}

TEST(TextKindSet, TogglesIndependently) {
  TextKindSet kinds;
  kinds.set(TextKind::Dimension, false);
  EXPECT_FALSE(kinds.visible(TextKind::Dimension));
  EXPECT_TRUE(kinds.visible(TextKind::AxisLabel));

  kinds.toggle(TextKind::AxisLabel);
  EXPECT_FALSE(kinds.visible(TextKind::AxisLabel));
  kinds.toggle(TextKind::AxisLabel);
  EXPECT_TRUE(kinds.visible(TextKind::AxisLabel));

  kinds.set_all(false);
  EXPECT_FALSE(kinds.visible(TextKind::StoreyLabel));
  kinds.set_all(true);
  EXPECT_TRUE(kinds.visible(TextKind::StoreyLabel));
}

}  // namespace
}  // namespace tamias
