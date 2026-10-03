#include "command/core/command_system.h"
#include "engine/document/document.h"
#include "host/session.h"
#include "host/session_mcp_backend.h"
#include "mcp/json.h"
#include "mcp/mcp_server.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {
namespace {

using mcp::Json;

Json make_request(std::string method, Json params, Json id) {
  return Json::object({
      {"jsonrpc", Json::string("2.0")},
      {"id", std::move(id)},
      {"method", Json::string(std::move(method))},
      {"params", std::move(params)},
  });
}

// 走完整链路：McpServer（协议）→ SessionMcpBackend（工具语义）→ Session（命令 + 事务）。
class SessionMcpTest : public ::testing::Test {
 protected:
  void SetUp() override {
    register_commands(command_registry());
    session_ = std::make_unique<Session>(std::make_shared<Document>("mcp-test"));
    backend_ = std::make_unique<SessionMcpBackend>(session_.get());
    server_ = std::make_unique<mcp::McpServer>(*backend_, "tamias", "0.1.0");
  }

  // 发一个 tools/call，返回结果里的文本（能解析成 JSON 就返回 JSON，否则返回字符串）。
  Json call(std::string_view tool, Json args = Json::object()) {
    auto response = server_->handle(make_request(
        "tools/call",
        Json::object({{"name", Json::string(std::string(tool))}, {"arguments", std::move(args)}}),
        Json::integer(1)));
    if (!response || response->find("result") == nullptr) {
      return Json::string("<no result>");
    }
    const Json& content = *response->find("result")->find("content");
    const std::string text = content.items()[0].find("text")->as_string();
    auto parsed = Json::parse(text);
    return parsed ? *parsed : Json::string(text);
  }

  // 布尔视图：这个工具调用是不是错误结果。
  bool is_error(std::string_view tool, Json args = Json::object()) {
    auto response = server_->handle(make_request(
        "tools/call",
        Json::object({{"name", Json::string(std::string(tool))}, {"arguments", std::move(args)}}),
        Json::integer(1)));
    return response->find("result")->find("isError")->as_bool();
  }

  std::uint64_t create_column() {
    std::vector<std::uint64_t> before;
    for (const auto& [id, unused] : session_->document().entities()) {
      (void)unused;
      before.push_back(id);
    }
    EXPECT_TRUE(session_->dispatch("create_column", {}));
    auto done = session_->command_system().feed_point(Vec3{0.f, 0.f, 0.f});
    EXPECT_TRUE(done && *done);
    for (const auto& [entity_id, unused] : session_->document().entities()) {
      (void)unused;
      if (std::find(before.begin(), before.end(), entity_id) == before.end()) {
        return entity_id;
      }
    }
    return 0;
  }

  std::unique_ptr<Session> session_;
  std::unique_ptr<SessionMcpBackend> backend_;
  std::unique_ptr<mcp::McpServer> server_;
};

TEST_F(SessionMcpTest, ToolsAreListedWithSchemas) {
  auto response = server_->handle(make_request("tools/list", Json::object(), Json::integer(1)));
  ASSERT_TRUE(response);
  const Json& tools = *response->find("result")->find("tools");
  std::vector<std::string> names;
  for (const Json& tool : tools.items()) {
    names.push_back(tool.find("name")->as_string());
    EXPECT_TRUE(tool.find("inputSchema")->is_object());
  }
  EXPECT_NE(std::find(names.begin(), names.end(), "tamias_dispatch"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "tamias_get_features"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "tamias_batch"), names.end());
}

TEST_F(SessionMcpTest, DocumentInfoReportsCountsByKind) {
  create_column();
  const Json info = call("tamias_document_info");
  EXPECT_EQ(info.find("name")->as_string(), "mcp-test");
  EXPECT_EQ(info.find("entityCount")->as_int(), 1);
  EXPECT_EQ(info.find("byKind")->find("Column")->as_int(), 1);
}

TEST_F(SessionMcpTest, ListEntitiesFiltersAndPaginates) {
  create_column();

  const Json all = call("tamias_list_entities");
  EXPECT_EQ(all.find("total")->as_int(), 1);
  EXPECT_EQ(all.find("entities")->items()[0].find("kind")->as_string(), "Column");

  const Json filtered =
      call("tamias_list_entities", Json::object({{"kind", Json::string("Wall")}}));
  EXPECT_EQ(filtered.find("total")->as_int(), 0);
}

TEST_F(SessionMcpTest, GetFeaturesReturnsFeatureTree) {
  const std::uint64_t id = create_column();
  const Json features =
      call("tamias_get_features", Json::object({{"entityId", Json::integer((std::int64_t)id)}}));
  EXPECT_EQ(features.find("entityId")->as_int(), (std::int64_t)id);
  EXPECT_TRUE(features.find("features")->is_array());
}

TEST_F(SessionMcpTest, MissingEntityIsAnErrorResult) {
  EXPECT_TRUE(is_error("tamias_get_features",
                       Json::object({{"entityId", Json::integer(999)}})));
}

TEST_F(SessionMcpTest, DispatchCreatesWallFromPoints) {
  Json points = Json::array();
  points.push_back(Json::object({{"x", Json::number(0)}, {"y", Json::number(0)}, {"z", Json::number(0)}}));
  points.push_back(Json::object({{"x", Json::number(5)}, {"y", Json::number(0)}, {"z", Json::number(0)}}));
  const Json result = call("tamias_dispatch",
                           Json::object({{"command", Json::string("create_wall")},
                                         {"args", Json::object({{"points", points}})}}));
  EXPECT_EQ(result.find("dispatched")->as_string(), "create_wall");
  EXPECT_FALSE(result.find("armed")->as_bool());
  EXPECT_EQ(call("tamias_document_info").find("entityCount")->as_int(), 1);
}

TEST_F(SessionMcpTest, WallWithoutPointsArmsTheToolForTheUser) {
  const Json result =
      call("tamias_dispatch", Json::object({{"command", Json::string("create_wall")},
                                            {"args", Json::object({{"thickness", Json::number(0.3)}})}}));
  EXPECT_TRUE(result.find("armed")->as_bool());
  EXPECT_EQ(call("tamias_document_info").find("entityCount")->as_int(), 0);
  session_->command_system().cancel();
}

TEST_F(SessionMcpTest, SetSelectionRoundTrips) {
  const std::uint64_t id = create_column();
  call("tamias_set_selection", Json::object({{"ids", Json::array()}}));
  EXPECT_TRUE(call("tamias_get_selection").find("ids")->empty());

  Json ids = Json::array();
  ids.push_back(Json::integer((std::int64_t)id));
  call("tamias_set_selection", Json::object({{"ids", ids}}));
  const Json selection = call("tamias_get_selection");
  ASSERT_EQ(selection.find("ids")->size(), 1u);
  EXPECT_EQ(selection.find("ids")->items()[0].as_int(), (std::int64_t)id);
}

TEST_F(SessionMcpTest, BatchIsOneUndoStep) {
  const std::uint64_t first = create_column();
  const std::uint64_t second = create_column();
  ASSERT_NE(first, second);

  Json operations = Json::array();
  for (const std::uint64_t id : {first, second}) {
    operations.push_back(Json::object(
        {{"command", Json::string("delete_entity")},
         {"args", Json::object({{"entity_id", Json::integer((std::int64_t)id)}})}}));
  }
  const Json result = call("tamias_batch", Json::object({{"operations", operations}}));
  EXPECT_EQ(result.find("executed")->as_int(), 2);
  EXPECT_EQ(call("tamias_document_info").find("entityCount")->as_int(), 0);

  // 一条事务 = 一步撤销：撤一次两个都回来。
  EXPECT_TRUE(session_->can_undo());
  session_->undo();
  EXPECT_EQ(call("tamias_document_info").find("entityCount")->as_int(), 2);
}

TEST_F(SessionMcpTest, BatchRollsBackOnFailure) {
  const std::uint64_t id = create_column();
  Json operations = Json::array();
  operations.push_back(Json::object(
      {{"command", Json::string("delete_entity")},
       {"args", Json::object({{"entity_id", Json::integer((std::int64_t)id)}})}}));
  operations.push_back(
      Json::object({{"command", Json::string("no_such_command")}, {"args", Json::object()}}));

  EXPECT_TRUE(is_error("tamias_batch", Json::object({{"operations", operations}})));
  // 回滚：那条删除被撤销，实体还在。
  EXPECT_EQ(call("tamias_document_info").find("entityCount")->as_int(), 1);
  // 撤销栈上一条应该是「建柱」，不是失败的批次——所以撤一次实体就没了。
  session_->undo();
  EXPECT_EQ(call("tamias_document_info").find("entityCount")->as_int(), 0);
}

TEST_F(SessionMcpTest, DispatchReportsUnknownCommand) {
  EXPECT_TRUE(is_error("tamias_dispatch",
                       Json::object({{"command", Json::string("no_such_command")}})));
}

TEST_F(SessionMcpTest, SetParamRejectsStringThatBreaksTheTextProtocol) {
  const std::uint64_t id = create_column();
  EXPECT_TRUE(is_error("tamias_set_param",
                       Json::object({{"entityId", Json::integer((std::int64_t)id)},
                                     {"featureId", Json::integer(1)},
                                     {"paramName", Json::string("a;b")},
                                     {"value", Json::number(1)}})));
}

TEST_F(SessionMcpTest, UndoRedoToolsDriveTheCommandStack) {
  create_column();
  const Json undone = call("tamias_undo");
  EXPECT_FALSE(undone.find("canUndo")->as_bool());
  EXPECT_TRUE(undone.find("canRedo")->as_bool());
  EXPECT_EQ(call("tamias_document_info").find("entityCount")->as_int(), 0);

  const Json redone = call("tamias_redo");
  EXPECT_TRUE(redone.find("canUndo")->as_bool());
  EXPECT_EQ(call("tamias_document_info").find("entityCount")->as_int(), 1);
}

TEST_F(SessionMcpTest, CommandCatalogResourceIsReadable) {
  ASSERT_TRUE(backend_->read_resource("tamias://commands").has_value());
  auto response = server_->handle(make_request(
      "resources/read", Json::object({{"uri", Json::string("tamias://commands")}}),
      Json::integer(2)));
  ASSERT_TRUE(response);
  const std::string text =
      response->find("result")->find("contents")->items()[0].find("text")->as_string();
  auto catalog = Json::parse(text);
  ASSERT_TRUE(catalog);
  EXPECT_GT(catalog->size(), 20u);
}

TEST_F(SessionMcpTest, EntityFeaturesTemplateIsReadable) {
  const std::uint64_t id = create_column();
  const std::string uri = "tamias://entity/" + std::to_string(id) + "/features";
  EXPECT_TRUE(backend_->read_resource(uri).has_value());
  EXPECT_FALSE(backend_->read_resource("tamias://entity/99999/features").has_value());
}

TEST_F(SessionMcpTest, EvaluateToolIsHiddenUnlessEnabled) {
  auto response = server_->handle(make_request(
      "tools/call",
      Json::object({{"name", Json::string("tamias_evaluate")},
                    {"arguments", Json::object({{"code", Json::string("1 + 1")}})}}),
      Json::integer(1)));
  ASSERT_TRUE(response);
  // 没打开时它根本不在工具表里：协议层按「未知工具」拒绝，模型不会误以为能用。
  EXPECT_EQ(response->find("error")->find("code")->as_int(), mcp::McpServer::kInvalidParams);
}

TEST_F(SessionMcpTest, EvaluateToolRunsWhenEnabled) {
  backend_->set_evaluate([](std::string_view code) { return std::string(code) + " -> 42"; });
  const Json result = call("tamias_evaluate", Json::object({{"code", Json::string("6 * 7")}}));
  EXPECT_EQ(result.find("result")->as_string(), "6 * 7 -> 42");
}

}  // namespace
}  // namespace tamias
