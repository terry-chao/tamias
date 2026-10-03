#include "mcp/json.h"
#include "mcp/mcp_backend.h"
#include "mcp/mcp_server.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tamias::mcp {
namespace {

// 门面的协议层不认识 Session / Document：假后端就够把 JSON-RPC 与 MCP 方法测全。
// 真后端（SessionMcpBackend）只负责「工具语义」，协议问题应该在这里挡住。
class FakeBackend final : public McpBackend {
 public:
  [[nodiscard]] std::vector<McpTool> tools() const override {
    return {
        McpTool{"echo", "回显参数", schema::object("", {schema::Property{"value", schema::freeform("任意值")}})},
        McpTool{"boom", "总是失败", schema::object("", {})},
    };
  }

  [[nodiscard]] std::vector<McpResource> resources() const override {
    return {McpResource{"tamias://document", "文档", "当前文档摘要", "application/json"}};
  }

  [[nodiscard]] std::vector<McpResourceTemplate> resource_templates() const override {
    return {McpResourceTemplate{"tamias://entity/{entityId}/features", "特征树",
                                "某个实体的特征树", "application/json"}};
  }

  [[nodiscard]] McpToolResult call_tool(std::string_view name, const Json& args) override {
    last_tool = std::string(name);
    last_args = args;
    if (name == "echo") {
      return McpToolResult{false, args.dump()};
    }
    if (name == "boom") {
      return McpToolResult{true, "exploded"};
    }
    return McpToolResult{true, "unexpected tool"};
  }

  [[nodiscard]] std::optional<McpResourceContent> read_resource(
      std::string_view uri) override {
    if (uri == "tamias://document") {
      return McpResourceContent{"tamias://document", "application/json", R"({"name":"doc"})"};
    }
    return std::nullopt;
  }

  std::string last_tool;
  Json last_args;
};

Json make_request(std::string method, Json params, Json id) {
  return Json::object({
      {"jsonrpc", Json::string("2.0")},
      {"id", std::move(id)},
      {"method", Json::string(std::move(method))},
      {"params", std::move(params)},
  });
}

class McpServerTest : public ::testing::Test {
 protected:
  McpServerTest() : server_(backend_, "tamias", "0.1.0") {}

  FakeBackend backend_;
  McpServer server_;
};

// —— JSON ——

TEST(JsonTest, ParsesAndRoundTrips) {
  std::string error;
  auto parsed = Json::parse(R"({"a":1,"b":[true,null,"x\u00e9"],"c":1.5,"d":{"e":-2}})", &error);
  ASSERT_TRUE(parsed) << error;
  EXPECT_TRUE(parsed->find("a")->is_int());
  EXPECT_EQ(parsed->find("a")->as_int(), 1);
  EXPECT_EQ(parsed->find("b")->size(), 3u);
  EXPECT_EQ(parsed->find("b")->items()[2].as_string(), std::string("x\xc3\xa9"));
  EXPECT_DOUBLE_EQ(parsed->find("c")->as_double(), 1.5);
  EXPECT_EQ(parsed->find("d")->find("e")->as_int(), -2);
  // 往返：再解析一遍 dump 出来的文本，值不变。
  auto again = Json::parse(parsed->dump(), &error);
  ASSERT_TRUE(again) << error;
  EXPECT_EQ(again->dump(), parsed->dump());
}

TEST(JsonTest, EscapeRoundTrip) {
  const std::string original = "quote\" back\\slash\nnew\ttab";
  auto parsed = Json::parse(Json::string(original).dump());
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->as_string(), original);
}

TEST(JsonTest, RejectsMalformedInput) {
  std::string error;
  EXPECT_FALSE(Json::parse("{\"a\":1", &error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(Json::parse("[1,2,]"));
  EXPECT_FALSE(Json::parse("{\"a\":1} trailing"));
  EXPECT_FALSE(Json::parse("nul"));
}

// —— MCP 握手 ——

TEST_F(McpServerTest, InitializeReportsServerInfoAndCapabilities) {
  auto response = server_.handle(make_request(
      "initialize", Json::object({{"protocolVersion", Json::string("2025-06-18")}}),
      Json::integer(1)));
  ASSERT_TRUE(response);
  const Json& result = *response->find("result");
  EXPECT_EQ(result.find("protocolVersion")->as_string(), "2025-06-18");
  EXPECT_EQ(result.find("serverInfo")->find("name")->as_string(), "tamias");
  EXPECT_EQ(result.find("serverInfo")->find("version")->as_string(), "0.1.0");
  EXPECT_TRUE(result.find("capabilities")->contains("tools"));
  EXPECT_TRUE(result.find("capabilities")->contains("resources"));
}

TEST_F(McpServerTest, InitializeEchoesKnownOlderVersion) {
  auto response = server_.handle(make_request(
      "initialize", Json::object({{"protocolVersion", Json::string("2024-11-05")}}),
      Json::integer(1)));
  ASSERT_TRUE(response);
  EXPECT_EQ(response->find("result")->find("protocolVersion")->as_string(), "2024-11-05");
}

TEST_F(McpServerTest, InitializeFallsBackToLatestForUnknownVersion) {
  auto response = server_.handle(make_request(
      "initialize", Json::object({{"protocolVersion", Json::string("1999-01-01")}}),
      Json::integer(1)));
  ASSERT_TRUE(response);
  EXPECT_EQ(response->find("result")->find("protocolVersion")->as_string(),
            McpServer::supported_protocol_version());
}

TEST_F(McpServerTest, PingReturnsEmptyResult) {
  auto response = server_.handle(make_request("ping", Json::object(), Json::integer(7)));
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->contains("result"));
  EXPECT_TRUE(response->find("result")->is_object());
}

// —— 工具 ——

TEST_F(McpServerTest, ToolsListExposesSchemas) {
  auto response = server_.handle(make_request("tools/list", Json::object(), Json::integer(2)));
  ASSERT_TRUE(response);
  const Json& tools = *response->find("result")->find("tools");
  ASSERT_EQ(tools.size(), 2u);
  EXPECT_EQ(tools.items()[0].find("name")->as_string(), "echo");
  EXPECT_TRUE(tools.items()[0].find("inputSchema")->is_object());
}

TEST_F(McpServerTest, ToolsCallPassesArgsToBackend) {
  Json args = Json::object({{"value", Json::integer(42)}});
  auto response = server_.handle(
      make_request("tools/call",
                   Json::object({{"name", Json::string("echo")}, {"arguments", args}}),
                   Json::integer(3)));
  ASSERT_TRUE(response);
  EXPECT_EQ(backend_.last_tool, "echo");
  EXPECT_EQ(backend_.last_args.find("value")->as_int(), 42);
  const Json& result = *response->find("result");
  EXPECT_FALSE(result.find("isError")->as_bool());
  EXPECT_EQ(result.find("content")->items()[0].find("type")->as_string(), "text");
  EXPECT_EQ(result.find("content")->items()[0].find("text")->as_string(), args.dump());
}

TEST_F(McpServerTest, ToolExecutionFailureIsAResultNotAProtocolError) {
  auto response = server_.handle(
      make_request("tools/call", Json::object({{"name", Json::string("boom")}}),
                   Json::integer(4)));
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->contains("result"));
  EXPECT_TRUE(response->find("result")->find("isError")->as_bool());
  EXPECT_EQ(response->find("result")->find("content")->items()[0].find("text")->as_string(),
            "exploded");
}

TEST_F(McpServerTest, UnknownToolIsInvalidParams) {
  auto response = server_.handle(
      make_request("tools/call", Json::object({{"name", Json::string("nope")}}),
                   Json::integer(5)));
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->contains("error"));
  EXPECT_EQ(response->find("error")->find("code")->as_int(), McpServer::kInvalidParams);
}

TEST_F(McpServerTest, ToolsCallWithoutNameIsInvalidParams) {
  auto response = server_.handle(make_request("tools/call", Json::object(), Json::integer(6)));
  ASSERT_TRUE(response);
  EXPECT_EQ(response->find("error")->find("code")->as_int(), McpServer::kInvalidParams);
}

// —— 资源 ——

TEST_F(McpServerTest, ResourcesListAndRead) {
  auto list = server_.handle(make_request("resources/list", Json::object(), Json::integer(8)));
  ASSERT_TRUE(list);
  const Json& resources = *list->find("result")->find("resources");
  ASSERT_EQ(resources.size(), 1u);
  EXPECT_EQ(resources.items()[0].find("uri")->as_string(), "tamias://document");

  auto read = server_.handle(make_request(
      "resources/read", Json::object({{"uri", Json::string("tamias://document")}}),
      Json::integer(9)));
  ASSERT_TRUE(read);
  const Json& contents = *read->find("result")->find("contents");
  EXPECT_EQ(contents.items()[0].find("text")->as_string(), R"({"name":"doc"})");
}

TEST_F(McpServerTest, UnknownResourceIsInvalidParams) {
  auto response = server_.handle(make_request(
      "resources/read", Json::object({{"uri", Json::string("tamias://nope")}}),
      Json::integer(10)));
  ASSERT_TRUE(response);
  EXPECT_EQ(response->find("error")->find("code")->as_int(), McpServer::kInvalidParams);
}

TEST_F(McpServerTest, ResourceTemplatesAreListed) {
  auto response =
      server_.handle(make_request("resources/templates/list", Json::object(), Json::integer(14)));
  ASSERT_TRUE(response);
  const Json& templates = *response->find("result")->find("resourceTemplates");
  ASSERT_EQ(templates.size(), 1u);
  EXPECT_EQ(templates.items()[0].find("uriTemplate")->as_string(),
            "tamias://entity/{entityId}/features");
}

// —— 协议边界 ——

TEST_F(McpServerTest, NotificationGetsNoReply) {
  Json notification = Json::object({
      {"jsonrpc", Json::string("2.0")},
      {"method", Json::string("notifications/initialized")},
  });
  EXPECT_FALSE(server_.handle(notification));
  EXPECT_FALSE(server_.handle(make_request("notifications/cancelled", Json::object(),
                                           Json::integer(11))));
}

TEST_F(McpServerTest, UnknownMethodIsMethodNotFound) {
  auto response = server_.handle(make_request("does/not/exist", Json::object(), Json::integer(12)));
  ASSERT_TRUE(response);
  EXPECT_EQ(response->find("error")->find("code")->as_int(), McpServer::kMethodNotFound);
}

TEST_F(McpServerTest, WrongJsonrpcVersionIsInvalidRequest) {
  Json bad = Json::object({
      {"jsonrpc", Json::string("1.0")},
      {"id", Json::integer(13)},
      {"method", Json::string("ping")},
  });
  auto response = server_.handle(bad);
  ASSERT_TRUE(response);
  EXPECT_EQ(response->find("error")->find("code")->as_int(), McpServer::kInvalidRequest);
}

TEST_F(McpServerTest, HandleTextReportsParseError) {
  auto response = server_.handle_text("{not json");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->find("error")->find("code")->as_int(), McpServer::kParseError);
  EXPECT_TRUE(response->find("id")->is_null());
}

TEST_F(McpServerTest, ResponseIdKeepsItsType) {
  auto string_id = server_.handle(make_request("ping", Json::object(), Json::string("abc")));
  ASSERT_TRUE(string_id);
  ASSERT_TRUE(string_id->find("id")->is_string());
  EXPECT_EQ(string_id->find("id")->as_string(), "abc");

  auto int_id = server_.handle(make_request("ping", Json::object(), Json::integer(3)));
  ASSERT_TRUE(int_id);
  ASSERT_TRUE(int_id->find("id")->is_int());
  EXPECT_EQ(int_id->find("id")->as_int(), 3);
}

TEST_F(McpServerTest, BatchCollectsResponsesAndSkipsNotifications) {
  Json batch = Json::array();
  batch.push_back(make_request("ping", Json::object(), Json::integer(1)));
  batch.push_back(Json::object({
      {"jsonrpc", Json::string("2.0")},
      {"method", Json::string("notifications/initialized")},
  }));
  batch.push_back(make_request("tools/list", Json::object(), Json::integer(2)));

  auto response = server_.handle(batch);
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->is_array());
  ASSERT_EQ(response->size(), 2u);
  EXPECT_EQ(response->items()[0].find("id")->as_int(), 1);
  EXPECT_EQ(response->items()[1].find("id")->as_int(), 2);
}

// —— 闸门与审计 ——

TEST_F(McpServerTest, DeniedToolIsAResultAndTheBackendIsNeverCalled) {
  bool observed = false;
  server_.set_tool_gate([](std::string_view, const Json&) {
    return McpToolGateDecision{false, "read-only mode"};
  });
  server_.set_tool_observer(
      [&](std::string_view tool, const Json&, const McpToolResult& result) {
        observed = true;
        EXPECT_EQ(tool, "echo");
        EXPECT_TRUE(result.is_error);
        EXPECT_EQ(result.text, "read-only mode");
      });

  auto response = server_.handle(make_request(
      "tools/call", Json::object({{"name", Json::string("echo")}}), Json::integer(20)));
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->contains("result"));  // 拒绝是工具结果，不是协议错误
  EXPECT_TRUE(response->find("result")->find("isError")->as_bool());
  EXPECT_EQ(response->find("result")->find("content")->items()[0].find("text")->as_string(),
            "read-only mode");
  EXPECT_TRUE(observed);
  EXPECT_TRUE(backend_.last_tool.empty());
}

TEST_F(McpServerTest, AllowedToolRunsAndTheObserverSeesSuccess) {
  int gate_calls = 0;
  server_.set_tool_gate([&](std::string_view, const Json&) {
    ++gate_calls;
    return McpToolGateDecision{};
  });
  McpToolResult seen;
  server_.set_tool_observer(
      [&](std::string_view, const Json&, const McpToolResult& result) { seen = result; });

  auto response = server_.handle(make_request(
      "tools/call",
      Json::object({{"name", Json::string("echo")},
                    {"arguments", Json::object({{"value", Json::integer(1)}})}}),
      Json::integer(21)));
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->find("result")->find("isError")->as_bool());
  EXPECT_EQ(gate_calls, 1);
  EXPECT_FALSE(seen.is_error);
  EXPECT_EQ(backend_.last_tool, "echo");
}

TEST_F(McpServerTest, UnknownToolIsRejectedBeforeTheGateIsConsulted) {
  bool gate_called = false;
  server_.set_tool_gate([&](std::string_view, const Json&) {
    gate_called = true;
    return McpToolGateDecision{};
  });
  auto response = server_.handle(make_request(
      "tools/call", Json::object({{"name", Json::string("nope")}}), Json::integer(22)));
  ASSERT_TRUE(response);
  EXPECT_EQ(response->find("error")->find("code")->as_int(), McpServer::kInvalidParams);
  EXPECT_FALSE(gate_called);
}

}  // namespace
}  // namespace tamias::mcp
