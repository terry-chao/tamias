#include "command/command_system.h"
#include "engine/document/document.h"
#include "host/session.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

namespace tamias {
namespace {

std::shared_ptr<Document> make_doc() {
  return std::make_shared<Document>("session-test");
}

TEST(SessionTest, DispatchUndoRedo) {
  register_commands(command_registry());
  Session session(make_doc());
  ASSERT_TRUE(session.dispatch("create_box", {}));
  // create_box 是单点交互命令：喂一个点完成放置。
  auto done = session.command_system().feed_point(Vec3{0.f, 0.f, 0.f});
  ASSERT_TRUE(done && *done);
  EXPECT_EQ(session.document().entities().size(), 1u);
  EXPECT_TRUE(session.can_undo());
  EXPECT_FALSE(session.can_redo());

  session.undo();
  EXPECT_EQ(session.document().entities().size(), 0u);
  EXPECT_FALSE(session.can_undo());
  EXPECT_TRUE(session.can_redo());

  session.redo();
  EXPECT_EQ(session.document().entities().size(), 1u);
}

TEST(SessionTest, SelectionHelpers) {
  register_commands(command_registry());
  Session session(make_doc());
  ASSERT_TRUE(session.dispatch("create_box", {}));
  auto done = session.command_system().feed_point(Vec3{0.f, 0.f, 0.f});
  ASSERT_TRUE(done && *done);
  ASSERT_FALSE(session.document().entities().empty());
  const std::uint64_t id = session.document().entities().begin()->first;

  EXPECT_TRUE(session.selection().empty());
  session.select(id);
  ASSERT_EQ(session.selection().size(), 1u);
  EXPECT_EQ(session.selection()[0], id);

  session.deselect(id);
  EXPECT_TRUE(session.selection().empty());

  session.set_selection({id});
  ASSERT_EQ(session.selection().size(), 1u);
  session.clear_selection();
  EXPECT_TRUE(session.selection().empty());
}

TEST(SessionTest, ToolMode) {
  Session session(make_doc());
  EXPECT_EQ(session.tool_mode(), ToolMode::None);
  session.set_tool(ToolMode::Wall);
  EXPECT_EQ(session.tool_mode(), ToolMode::Wall);
  session.set_tool(ToolMode::None);
  EXPECT_EQ(session.tool_mode(), ToolMode::None);
}

TEST(SessionTest, ResetDocumentClearsCommandStack) {
  register_commands(command_registry());
  Session session(make_doc());
  ASSERT_TRUE(session.dispatch("create_box", {}));
  auto done = session.command_system().feed_point(Vec3{0.f, 0.f, 0.f});
  ASSERT_TRUE(done && *done);
  EXPECT_TRUE(session.can_undo());

  session.reset_document(make_doc());
  EXPECT_FALSE(session.can_undo());
  EXPECT_FALSE(session.can_redo());
  EXPECT_TRUE(session.document().entities().empty());
}

}  // namespace
}  // namespace tamias
