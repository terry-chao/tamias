#include "command/core/command_system.h"
#include "engine/document/document.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace tamias {
namespace {

struct Cmd {
  CommandRegistry registry;
  CommandSystem system;
  Document doc;

  explicit Cmd(const char* name) : system(registry), doc(name) { register_commands(registry); }
};

CommandArgs column_at(float x) {
  return {{"origin", Vec3{x, 0.f, 0.f}},
          {"width", 0.4},
          {"depth", 0.4},
          {"height", 3.0}};
}

// 事务存在的唯一理由：脚本一次改 N 个参数，用户按一次 Ctrl+Z 就该全部退回。
TEST(Transaction, GroupsDispatchesIntoOneUndoStep) {
  Cmd cmd("tx");
  ASSERT_TRUE(cmd.system.begin_transaction("批量"));
  EXPECT_TRUE(cmd.system.in_transaction());
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_column", column_at(static_cast<float>(i))));
  }
  EXPECT_EQ(cmd.system.transaction_size(), 3u);
  EXPECT_EQ(cmd.doc.entities().size(), 3u);
  // 还没提交：撤销栈上一个字都没有，中途按 Ctrl+Z 不会撤到一半。
  EXPECT_FALSE(cmd.system.can_undo());

  ASSERT_TRUE(cmd.system.commit_transaction());
  EXPECT_FALSE(cmd.system.in_transaction());
  EXPECT_TRUE(cmd.system.can_undo());

  cmd.system.undo();
  EXPECT_TRUE(cmd.doc.entities().empty());
  EXPECT_FALSE(cmd.system.can_undo()) << "三条命令应当只占一步";
  ASSERT_TRUE(cmd.system.can_redo());
  cmd.system.redo();
  EXPECT_EQ(cmd.doc.entities().size(), 3u);
}

TEST(Transaction, AbortRollsBackWithoutUndoEntry) {
  Cmd cmd("tx-abort");
  ASSERT_TRUE(cmd.system.begin_transaction());
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_column", column_at(0.f)));
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_column", column_at(1.f)));
  EXPECT_EQ(cmd.doc.entities().size(), 2u);

  auto rolled = cmd.system.abort_transaction();
  ASSERT_TRUE(rolled);
  EXPECT_EQ(*rolled, 2u);
  EXPECT_TRUE(cmd.doc.entities().empty());
  EXPECT_FALSE(cmd.system.can_undo());
  EXPECT_FALSE(cmd.system.can_redo());
  EXPECT_FALSE(cmd.system.in_transaction());
}

TEST(Transaction, EmptyCommitLeavesNoUndoEntry) {
  Cmd cmd("tx-empty");
  ASSERT_TRUE(cmd.system.begin_transaction("空"));
  ASSERT_TRUE(cmd.system.commit_transaction());
  EXPECT_FALSE(cmd.system.can_undo());
  EXPECT_FALSE(cmd.system.in_transaction());
}

TEST(Transaction, RejectsNestingAndMisuse) {
  Cmd cmd("tx-bad");
  EXPECT_FALSE(cmd.system.commit_transaction());  // 没开事务
  EXPECT_FALSE(cmd.system.abort_transaction());

  ASSERT_TRUE(cmd.system.begin_transaction());
  EXPECT_FALSE(cmd.system.begin_transaction()) << "不支持嵌套";

  // 事务里不能武装交互式命令：点齐的时刻由鼠标决定，不在事务窗口里。
  auto armed = cmd.system.dispatch(cmd.doc, "create_wall",
                                   {{"thickness", 0.2}, {"height", 3.0}});
  EXPECT_FALSE(armed);
  EXPECT_FALSE(cmd.system.has_pending());
  ASSERT_TRUE(cmd.system.abort_transaction());

  // 事务外武装照常。
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_wall",
                                  {{"thickness", 0.2}, {"height", 3.0}}));
  EXPECT_TRUE(cmd.system.has_pending());
}

// 换文档时 clear()：事务和它缓冲的命令一起丢掉，绝不回头 undo——
// 那些命令持有的是旧文档的指针。
TEST(Transaction, ClearDiscardsOpenTransaction) {
  Cmd cmd("tx-clear");
  ASSERT_TRUE(cmd.system.begin_transaction());
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_column", column_at(0.f)));
  ASSERT_EQ(cmd.doc.entities().size(), 1u);

  cmd.system.clear();
  EXPECT_FALSE(cmd.system.in_transaction());
  EXPECT_FALSE(cmd.system.can_undo());
  EXPECT_EQ(cmd.doc.entities().size(), 1u);
}

}  // namespace
}  // namespace tamias
