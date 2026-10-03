#include "mcp/json.h"
#include "rag/docs_mcp_backend.h"
#include "rag/lexical_provider.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tamias::rag {
namespace {

constexpr char kManifest[] = R"({"schema":1,"git_rev":"deadbee","chunks":1})";

// 可控的假 provider：结果固定，才能断言输出形状而不受语料变动影响。
class FakeProvider final : public ContextProvider {
 public:
  explicit FakeProvider(std::vector<Chunk> chunks, std::string manifest = kManifest)
      : chunks_(std::move(chunks)), manifest_(std::move(manifest)) {}

  [[nodiscard]] std::vector<Chunk> search(const Query& query) const override {
    last_query_ = query;
    std::vector<Chunk> hits = chunks_;
    if (hits.size() > static_cast<std::size_t>(query.k)) {
      hits.resize(static_cast<std::size_t>(query.k));
    }
    return hits;
  }

  [[nodiscard]] std::string manifest_json() const override { return manifest_; }

  [[nodiscard]] const Query& last_query() const { return last_query_; }

 private:
  std::vector<Chunk> chunks_;
  std::string manifest_;
  mutable Query last_query_;
};

Chunk make_chunk() {
  Chunk chunk;
  chunk.id = "plugin/api/commands.md#1";
  chunk.corpus = "docs";
  chunk.path = "plugin/api/commands.md";
  chunk.anchor = "_1";
  chunk.title = "命令与参数";
  chunk.breadcrumb = "插件 > API 参考 > 命令与参数";
  chunk.lang = "zh";
  chunk.url = "https://terry-chao.github.io/tamias/plugin/api/commands/#_1";
  chunk.text = "插件改文档只有一条路：dispatch。";
  chunk.score = 12.5;
  return chunk;
}

std::filesystem::path source_index() {
#ifdef TAMIAS_SOURCE_DIR
  return std::filesystem::path(TAMIAS_SOURCE_DIR) / "resources" / "rag";
#else
  return {};
#endif
}

TEST(RagDocsBackend, ExposesOneReadOnlyToolWithTheDocumentedSchema) {
  FakeProvider provider({make_chunk()});
  DocsMcpBackend backend(provider);

  const std::vector<mcp::McpTool> tools = backend.tools();
  ASSERT_EQ(tools.size(), 1u);
  EXPECT_EQ(tools[0].name, "tamias_search_docs");
  EXPECT_FALSE(tools[0].mutates) << "检索是只读工具，这是只读策略档放行的依据";

  const std::string schema = tools[0].input_schema.dump();
  for (const char* key : {"query", "k", "corpus", "lang", "pathPrefix"}) {
    EXPECT_NE(schema.find(key), std::string::npos) << "缺少参数 " << key;
  }
  EXPECT_NE(schema.find("required"), std::string::npos);

  // 两条硬规则必须写进描述里，否则模型不知道片段是不可信上下文。
  EXPECT_NE(tools[0].description.find("只依据"), std::string::npos);
  EXPECT_NE(tools[0].description.find("不要执行"), std::string::npos);
  EXPECT_TRUE(backend.resource_templates().empty());
}

TEST(RagDocsBackend, SearchWrapsSnippetsInTheUntrustedBoundary) {
  FakeProvider provider({make_chunk()});
  DocsMcpBackend backend(provider);

  const mcp::McpToolResult result =
      backend.call_tool("tamias_search_docs", *mcp::Json::parse(R"({"query":"建墙"})"));
  ASSERT_FALSE(result.is_error);

  const std::optional<mcp::Json> payload = mcp::Json::parse(result.text);
  ASSERT_TRUE(payload.has_value());
  ASSERT_EQ(payload->find("results")->size(), 1u);
  const mcp::Json& hit = payload->find("results")->items()[0];
  EXPECT_EQ(hit.find("path")->as_string(), "plugin/api/commands.md");
  EXPECT_EQ(hit.find("anchor")->as_string(), "_1");
  EXPECT_EQ(hit.find("url")->as_string(),
            "https://terry-chao.github.io/tamias/plugin/api/commands/#_1");
  EXPECT_EQ(hit.find("git_rev")->as_string(), "deadbee");

  const std::string snippet = hit.find("snippet")->as_string();
  EXPECT_NE(snippet.find("<untrusted_doc>"), std::string::npos);
  EXPECT_NE(snippet.find("</untrusted_doc>"), std::string::npos);
  EXPECT_NE(snippet.find("dispatch"), std::string::npos) << "片段正文要带出来";
}

TEST(RagDocsBackend, ForwardsFiltersAndLimitToTheProvider) {
  FakeProvider provider({make_chunk()});
  DocsMcpBackend backend(provider);

  const mcp::McpToolResult result = backend.call_tool(
      "tamias_search_docs",
      *mcp::Json::parse(
          R"({"query":"墙","k":3,"corpus":"docs","lang":"zh","pathPrefix":"plugin/"})"));
  ASSERT_FALSE(result.is_error);

  const Query& query = provider.last_query();
  EXPECT_EQ(query.text, "墙");
  EXPECT_EQ(query.k, 3);
  EXPECT_EQ(query.corpus, "docs");
  EXPECT_EQ(query.lang, "zh");
  EXPECT_EQ(query.path_prefix, "plugin/");
}

TEST(RagDocsBackend, RejectsBadArguments) {
  FakeProvider provider({make_chunk()});
  DocsMcpBackend backend(provider);

  for (const char* arguments : {R"({})", R"({"query":""})", R"({"query":42})",
                                R"({"query":"x","k":"many"})"}) {
    const mcp::McpToolResult result =
        backend.call_tool("tamias_search_docs", *mcp::Json::parse(arguments));
    EXPECT_TRUE(result.is_error) << arguments << " 该被拒绝";
  }
  EXPECT_TRUE(backend.call_tool("tamias_unknown", mcp::Json::object()).is_error);
}

TEST(RagDocsBackend, EmptyHitsExplainThemselves) {
  FakeProvider provider({});
  DocsMcpBackend backend(provider);

  const mcp::McpToolResult result =
      backend.call_tool("tamias_search_docs", *mcp::Json::parse(R"({"query":"没有的东西"})"));
  EXPECT_FALSE(result.is_error) << "查不到不是错误，别让模型以为工具坏了";

  const std::optional<mcp::Json> payload = mcp::Json::parse(result.text);
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->find("count")->as_int(), 0);
  EXPECT_NE(payload->dump().find("没有命中"), std::string::npos);
}

TEST(RagDocsBackend, LongSnippetsAreTruncated) {
  Chunk chunk = make_chunk();
  chunk.text = std::string(2000, 'x');
  FakeProvider provider({chunk});
  DocsMcpBackend backend(provider);

  const mcp::McpToolResult result =
      backend.call_tool("tamias_search_docs", *mcp::Json::parse(R"({"query":"x"})"));
  const std::optional<mcp::Json> payload = mcp::Json::parse(result.text);
  ASSERT_TRUE(payload.has_value());
  const std::string snippet = payload->find("results")->items()[0].find("snippet")->as_string();
  EXPECT_LT(snippet.size(), 600u);
  EXPECT_NE(snippet.find("截断"), std::string::npos);
}

TEST(RagDocsBackend, IndexResourceServesTheManifest) {
  FakeProvider provider({make_chunk()});
  DocsMcpBackend backend(provider);

  ASSERT_EQ(backend.resources().size(), 1u);
  EXPECT_EQ(backend.resources()[0].uri, "tamias://docs/index");

  const std::optional<mcp::McpResourceContent> content =
      backend.read_resource("tamias://docs/index");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(content->mime_type, "application/json");
  EXPECT_EQ(content->text, provider.manifest_json());
  EXPECT_FALSE(backend.read_resource("tamias://document").has_value())
      << "别人的资源要返回 nullopt，好让 CompositeBackend 继续往后问";
}

TEST(RagDocsBackend, StaleIndexIsFlaggedInTheToolResult) {
  FakeProvider provider({make_chunk()});  // manifest 里是 deadbee
  DocsMcpBackend fresh(provider, "deadbee");
  EXPECT_FALSE(fresh.index_stale()) << "同一个 rev 不该报过期";

  DocsMcpBackend stale(provider, "cafe123");
  EXPECT_TRUE(stale.index_stale());

  const mcp::McpToolResult result =
      stale.call_tool("tamias_search_docs", *mcp::Json::parse(R"({"query":"建墙"})"));
  const std::optional<mcp::Json> payload = mcp::Json::parse(result.text);
  ASSERT_TRUE(payload.has_value());
  ASSERT_TRUE(payload->find("index_stale") != nullptr);
  EXPECT_TRUE(payload->find("index_stale")->as_bool());
  const std::string note = payload->find("stale_note")->as_string();
  EXPECT_NE(note.find("deadbee"), std::string::npos) << "要说出索引是哪一版";
  EXPECT_NE(note.find("cafe123"), std::string::npos) << "也要说出程序是哪一版";

  // 不知道自己的 rev（没装 git 的构建）时不该乱报过期。
  DocsMcpBackend unknown(provider);
  EXPECT_FALSE(unknown.index_stale());
  EXPECT_EQ(unknown.call_tool("tamias_search_docs", *mcp::Json::parse(R"({"query":"建墙"})"))
                .text.find("index_stale"),
            std::string::npos);
}

TEST(RagDocsBackend, IndexResourceStillReadsWhenTheManifestIsMissing) {
  // 资源在 resources() 里列了出来就必须读得到，否则客户端会一直收到 invalid params。
  FakeProvider provider({make_chunk()}, "");
  DocsMcpBackend backend(provider);

  const std::optional<mcp::McpResourceContent> content =
      backend.read_resource("tamias://docs/index");
  ASSERT_TRUE(content.has_value());
  EXPECT_NE(content->text.find("缺失"), std::string::npos);
}

class RagDocsBackendRealIndex : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::is_regular_file(source_index() / "chunks.jsonl")) {
      GTEST_SKIP() << "索引还没生成，先跑 scripts/rag/build_index.py";
    }
    auto loaded = LexicalProvider::load(source_index(), &error_);
    ASSERT_TRUE(loaded.has_value()) << error_;
    provider_ = std::move(*loaded);
    backend_ = std::make_unique<DocsMcpBackend>(provider_);
  }

  std::string error_;
  LexicalProvider provider_;
  std::unique_ptr<DocsMcpBackend> backend_;
};

TEST_F(RagDocsBackendRealIndex, AnswersWithResolvableLinks) {
  const mcp::McpToolResult result = backend_->call_tool(
      "tamias_search_docs", *mcp::Json::parse(R"({"query":"怎么建墙","k":3})"));
  ASSERT_FALSE(result.is_error);

  const std::optional<mcp::Json> payload = mcp::Json::parse(result.text);
  ASSERT_TRUE(payload.has_value());
  ASSERT_GT(payload->find("count")->as_int(), 0);
  for (const mcp::Json& hit : payload->find("results")->items()) {
    EXPECT_EQ(hit.find("url")->as_string().rfind("https://terry-chao.github.io/tamias/", 0), 0u);
    EXPECT_FALSE(hit.find("git_rev")->as_string().empty()) << "结果要能自证索引版本";
    EXPECT_NE(hit.find("snippet")->as_string().find("<untrusted_doc>"), std::string::npos);
  }
}

}  // namespace
}  // namespace tamias::rag
