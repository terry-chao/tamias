#include "host/session_mcp_backend.h"
#include "mcp/composite_backend.h"
#include "mcp/mcp_server.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace tamias {
namespace {

// 合成后端的测试不需要真 Session：这里只关心「合流与路由」。
class FakeBackend final : public mcp::McpBackend {
 public:
  FakeBackend(std::string tag, std::string tool, std::string resource,
              std::string uri_template = {})
      : tag_(std::move(tag)),
        tool_(std::move(tool)),
        resource_(std::move(resource)),
        uri_template_(std::move(uri_template)) {}

  [[nodiscard]] std::vector<mcp::McpTool> tools() const override {
    if (tool_.empty()) {
      return {};
    }
    return {mcp::McpTool{tool_, "来自 " + tag_, mcp::Json::object(), false}};
  }

  [[nodiscard]] std::vector<mcp::McpResource> resources() const override {
    if (resource_.empty()) {
      return {};
    }
    return {mcp::McpResource{resource_, tag_, "描述", "application/json"}};
  }

  [[nodiscard]] std::vector<mcp::McpResourceTemplate> resource_templates() const override {
    if (uri_template_.empty()) {
      return {};
    }
    return {mcp::McpResourceTemplate{uri_template_, tag_, "描述", "application/json"}};
  }

  [[nodiscard]] mcp::McpToolResult call_tool(std::string_view name,
                                             const mcp::Json&) override {
    return mcp::McpToolResult{false, "handled:" + tag_ + ":" + std::string(name)};
  }

  [[nodiscard]] std::optional<mcp::McpResourceContent> read_resource(
      std::string_view uri) override {
    if (!uri_template_.empty() && uri.starts_with(kTemplatePrefix)) {
      return mcp::McpResourceContent{std::string(uri), "application/json",
                                     "templated:" + tag_};
    }
    if (uri == resource_) {
      return mcp::McpResourceContent{std::string(uri), "application/json", "exact:" + tag_};
    }
    return std::nullopt;
  }

  static constexpr std::string_view kTemplatePrefix = "tamias://entity/";

 private:
  std::string tag_;
  std::string tool_;
  std::string resource_;
  std::string uri_template_;
};

TEST(RagComposite, MergesToolsResourcesAndTemplates) {
  FakeBackend session("session", "tamias_document_info", "tamias://document");
  FakeBackend docs("docs", "tamias_search_docs", "tamias://docs/index",
                   "tamias://doc/{path}");
  mcp::CompositeBackend composite;
  ASSERT_TRUE(composite.add(session));
  ASSERT_TRUE(composite.add(docs));

  EXPECT_EQ(composite.backend_count(), 2u);
  EXPECT_EQ(composite.tools().size(), 2u);
  EXPECT_EQ(composite.resources().size(), 2u);
  EXPECT_EQ(composite.resource_templates().size(), 1u);
}

TEST(RagComposite, DuplicateToolNameIsRejectedAndNothingIsRegistered) {
  FakeBackend first("first", "tamias_undo", "tamias://a");
  FakeBackend clash("clash", "tamias_undo", "tamias://b");
  mcp::CompositeBackend composite;
  ASSERT_TRUE(composite.add(first));

  std::string error;
  EXPECT_FALSE(composite.add(clash, &error));
  EXPECT_EQ(error, "工具重名：tamias_undo");
  // 半途失败不能留下半个后端：工具表还是 1 个，`tamias://b` 也不该被登记。
  EXPECT_EQ(composite.backend_count(), 1u);
  EXPECT_EQ(composite.tools().size(), 1u);
  EXPECT_EQ(composite.resources().size(), 1u);
  EXPECT_FALSE(composite.read_resource("tamias://b").has_value());
}

TEST(RagComposite, DuplicateResourceUriAndTemplateAreRejected) {
  FakeBackend first("first", "tool.a", "tamias://same");
  FakeBackend same_resource("second", "tool.b", "tamias://same");
  mcp::CompositeBackend composite;
  ASSERT_TRUE(composite.add(first));

  std::string error;
  EXPECT_FALSE(composite.add(same_resource, &error));
  EXPECT_EQ(error, "资源重名：tamias://same");

  FakeBackend templated("third", "tool.c", "", "tamias://doc/{path}");
  FakeBackend same_template("fourth", "tool.d", "", "tamias://doc/{path}");
  mcp::CompositeBackend other;
  ASSERT_TRUE(other.add(templated));
  EXPECT_FALSE(other.add(same_template, &error));
  EXPECT_EQ(error, "资源模板重名：tamias://doc/{path}");
}

TEST(RagComposite, RoutesToolCallsToTheOwningBackend) {
  FakeBackend session("session", "tamias_document_info", "tamias://document");
  FakeBackend docs("docs", "tamias_search_docs", "tamias://docs/index");
  mcp::CompositeBackend composite;
  ASSERT_TRUE(composite.add(session));
  ASSERT_TRUE(composite.add(docs));

  EXPECT_EQ(composite.call_tool("tamias_search_docs", mcp::Json::object()).text,
            "handled:docs:tamias_search_docs");
  EXPECT_EQ(composite.call_tool("tamias_document_info", mcp::Json::object()).text,
            "handled:session:tamias_document_info");
}

TEST(RagComposite, UnknownToolIsAnErrorNotSilence) {
  FakeBackend session("session", "tamias_document_info", "tamias://document");
  mcp::CompositeBackend composite;
  ASSERT_TRUE(composite.add(session));

  const mcp::McpToolResult result = composite.call_tool("tamias_nope", mcp::Json::object());
  EXPECT_TRUE(result.is_error);
  EXPECT_NE(result.text.find("tamias_nope"), std::string::npos);
}

TEST(RagComposite, TemplatedResourceFallsBackPastTheExactRouteTable) {
  // 模板资源不在 resources() 里，精确路由表查不到，必须逐个后端问过去。
  FakeBackend session("session", "tamias_document_info", "tamias://document",
                      "tamias://entity/{entityId}/features");
  FakeBackend docs("docs", "tamias_search_docs", "tamias://docs/index");
  mcp::CompositeBackend composite;
  ASSERT_TRUE(composite.add(session));
  ASSERT_TRUE(composite.add(docs));

  const std::optional<mcp::McpResourceContent> exact =
      composite.read_resource("tamias://docs/index");
  ASSERT_TRUE(exact.has_value());
  EXPECT_EQ(exact->text, "exact:docs");

  const std::optional<mcp::McpResourceContent> templated =
      composite.read_resource("tamias://entity/7/features");
  ASSERT_TRUE(templated.has_value());
  EXPECT_EQ(templated->text, "templated:session");

  EXPECT_FALSE(composite.read_resource("tamias://nope").has_value());
}

TEST(RagComposite, SearchToolIsNotInTheMutatesListSoReadOnlyLetsItThrough) {
  // 这条是 M2 的验收点：闸门用 SessionMcpBackend::mutates 判读/写，而它只列了
  // 文档写工具 —— 检索工具不在名单里，所以「只读」档下天然放行，不需要审批。
  EXPECT_FALSE(SessionMcpBackend::mutates("tamias_search_docs"));
  EXPECT_TRUE(SessionMcpBackend::mutates("tamias_create_wall"));
}

TEST(RagComposite, ReadOnlyGateAdmitsSearchThroughTheWholeFacade) {
  FakeBackend session("session", "tamias_create_wall", "tamias://document");
  FakeBackend docs("docs", "tamias_search_docs", "tamias://docs/index");
  mcp::CompositeBackend composite;
  ASSERT_TRUE(composite.add(session));
  ASSERT_TRUE(composite.add(docs));

  mcp::McpServer server(composite, "tamias", "test");
  // 与 McpService::gate_tool 的只读档同一套判断。
  server.set_tool_gate([](std::string_view tool, const mcp::Json&) {
    if (!SessionMcpBackend::mutates(tool)) {
      return mcp::McpToolGateDecision{};
    }
    return mcp::McpToolGateDecision{false, "写操作被拒绝：Tamias 当前是只读策略。"};
  });

  const auto allow = mcp::Json::parse(
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call",)"
      R"("params":{"name":"tamias_search_docs","arguments":{}}})");
  ASSERT_TRUE(allow.has_value());
  const std::optional<mcp::Json> allowed = server.handle(*allow);
  ASSERT_TRUE(allowed.has_value());
  EXPECT_FALSE(allowed->find("error") != nullptr && !allowed->find("error")->is_null())
      << "检索工具在只读档下不该被闸门拦住";

  const auto deny = mcp::Json::parse(
      R"({"jsonrpc":"2.0","id":2,"method":"tools/call",)"
      R"("params":{"name":"tamias_create_wall","arguments":{}}})");
  ASSERT_TRUE(deny.has_value());
  const std::optional<mcp::Json> denied = server.handle(*deny);
  ASSERT_TRUE(denied.has_value());
  EXPECT_NE(denied->dump().find("只读"), std::string::npos)
      << "写工具在只读档下要带回拒绝原因";
}

TEST(RagComposite, EmptyCompositeIsHarmless) {
  mcp::CompositeBackend composite;
  EXPECT_TRUE(composite.empty());
  EXPECT_TRUE(composite.tools().empty());
  EXPECT_FALSE(composite.read_resource("tamias://anything").has_value());
  EXPECT_TRUE(composite.call_tool("tamias_anything", mcp::Json::object()).is_error);
}

}  // namespace
}  // namespace tamias
