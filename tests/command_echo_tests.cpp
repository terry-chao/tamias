#include "host/command_echo.h"

#include "command/core/command_system.h"
#include "engine/document/document.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace tamias {
namespace {

// 命令回显：内核只报「谁执行了、参数是什么」，这段文本是宿主给人抄的。
// 重点测三件事：参数全（六种 variant 都认）、顺序稳（CommandArgs 是无序表）、
// 交互式命令点齐后能补齐点（回显的是等价的一次性调用，不是「武装工具」那一步）。

TEST(CommandEcho, NoArgsOmitsCommandArgs) {
  EXPECT_EQ(format_dispatch_call("undo", {}), "host.Dispatch(\"undo\");");
}

TEST(CommandEcho, AllArgKindsInSortedOrder) {
  CommandArgs args;
  args.emplace("thickness", 0.2);
  args.emplace("height", 3.0);
  args.emplace("entity_id", static_cast<std::int64_t>(7));
  args.emplace("name", std::string("wall"));
  args.emplace("origin", Vec3{1.5f, 0.0f, -2.0f});
  args.emplace("points", std::vector<Vec3>{Vec3{0.f, 0.f, 0.f}, Vec3{5.f, 0.f, 0.f}});
  args.emplace("weights", std::vector<double>{1.0, 2.5});

  const std::string expected =
      "host.Dispatch(\"create_wall\", new CommandArgs()"
      ".SetInt(\"entity_id\", 7)"
      ".SetDouble(\"height\", 3)"
      ".SetString(\"name\", \"wall\")"
      ".SetVec3(\"origin\", 1.5f, 0f, -2f)"
      ".SetPoints(\"points\", [new PickPoint(0f, 0f, 0f, 0), new PickPoint(5f, 0f, 0f, 0)])"
      ".SetDouble(\"thickness\", 0.2)"
      ".SetDoubles(\"weights\", [1, 2.5]));";
  EXPECT_EQ(format_dispatch_call("create_wall", args), expected);
}

TEST(CommandEcho, EscapesStringsAndKeys) {
  CommandArgs args;
  args.emplace("say", std::string("a\"b\\c\nd"));
  EXPECT_EQ(format_dispatch_call("t\"x", args),
            "host.Dispatch(\"t\\\"x\", new CommandArgs()"
            ".SetString(\"say\", \"a\\\"b\\\\c\\nd\"));");
}

// 回显是从同一个 CommandSystem 出来的：非交互命令 dispatch 成功即回调一次。
TEST(CommandEcho, ObserverSeesExecutedCommand) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);
  Document doc("echo");

  std::string echoed_name;
  CommandArgs echoed_args;
  int calls = 0;
  system.set_observer([&](const std::string& name, const CommandArgs& args) {
    echoed_name = name;
    echoed_args = args;
    ++calls;
  });

  auto r = system.dispatch(doc, "create_column", {{"origin", Vec3{1.f, 0.f, 2.f}}});
  ASSERT_TRUE(r) << r.error();
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(echoed_name, "create_column");
  ASSERT_EQ(echoed_args.count("origin"), 1u);
  EXPECT_FLOAT_EQ(std::get<Vec3>(echoed_args.at("origin")).x, 1.f);
}

// 交互式命令：武装那一步不算「执行」，不回调；点齐后回调一次，
// 而且参数里既有武装参数（厚度 / 高度）也有交互采集到的点。
TEST(CommandEcho, InteractiveCommandEchoesCollectedPoints) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);
  Document doc("echo-wall");

  int calls = 0;
  CommandArgs echoed;
  system.set_observer([&](const std::string&, const CommandArgs& args) {
    echoed = args;
    ++calls;
  });

  auto armed = system.dispatch(doc, "create_wall", {{"thickness", 0.2}, {"height", 3.0}});
  ASSERT_TRUE(armed) << armed.error();
  EXPECT_TRUE(system.has_pending());
  EXPECT_EQ(calls, 0) << "武装工具不等于执行，不该回显";

  auto first = system.feed_point(Vec3{0.f, 0.f, 0.f});
  ASSERT_TRUE(first) << first.error();
  EXPECT_FALSE(*first);
  EXPECT_EQ(calls, 0);

  auto second = system.feed_point(Vec3{5.f, 0.f, 0.f});
  ASSERT_TRUE(second) << second.error();
  EXPECT_TRUE(*second);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(std::get<double>(echoed.at("thickness")), 0.2);
  EXPECT_EQ(std::get<double>(echoed.at("height")), 3.0);
  const auto& points = std::get<std::vector<Vec3>>(echoed.at("points"));
  ASSERT_EQ(points.size(), 2u);
  EXPECT_FLOAT_EQ(points[1].x, 5.f);
  EXPECT_EQ(doc.entities().size(), 1u);

  // 回显出来就该是完整的一次性调用（点给齐 → 不再武装）。
  EXPECT_EQ(format_dispatch_call("create_wall", echoed),
            "host.Dispatch(\"create_wall\", new CommandArgs()"
            ".SetDouble(\"height\", 3)"
            ".SetPoints(\"points\", [new PickPoint(0f, 0f, 0f, 0), new PickPoint(5f, 0f, 0f, 0)])"
            ".SetDouble(\"thickness\", 0.2));");
}

}  // namespace
}  // namespace tamias
