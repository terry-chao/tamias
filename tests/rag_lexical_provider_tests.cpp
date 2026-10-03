#include "rag/index_source.h"
#include "rag/lexical_provider.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace tamias::rag {
namespace {

std::filesystem::path source_index() {
#ifdef TAMIAS_SOURCE_DIR
  return std::filesystem::path(TAMIAS_SOURCE_DIR) / "resources" / "rag";
#else
  return {};
#endif
}

std::filesystem::path source_aliases() {
#ifdef TAMIAS_SOURCE_DIR
  return std::filesystem::path(TAMIAS_SOURCE_DIR) / "assets" / "rag" / "aliases.json";
#else
  return {};
#endif
}

bool paths_of(const std::vector<Chunk>& chunks, const std::string& path) {
  for (const Chunk& chunk : chunks) {
    if (chunk.path == path) {
      return true;
    }
  }
  return false;
}

TEST(RagLexicalProvider, MissingDirectoryReportsWhy) {
  std::string error;
  const auto provider =
      LexicalProvider::load(std::filesystem::path("no/such/index/directory"), &error);
  EXPECT_FALSE(provider.has_value());
  EXPECT_FALSE(error.empty());
  EXPECT_NE(error.find("chunks.jsonl"), std::string::npos);
}

TEST(RagIndexSource, ExplicitPathWinsAndFallsBackToTheSourceTree) {
  const IndexResolution explicit_hit = resolve_index_directory(source_index(), {});
  ASSERT_TRUE(explicit_hit.found.has_value()) << explicit_hit.error;
  EXPECT_EQ(explicit_hit.found->directory, source_index());
  EXPECT_EQ(explicit_hit.found->source, "命令行指定");

  // 给的路不存在时要掉到下一档，而不是直接失败 —— 开发机上直接跑构建产物
  // 走的就是「源码目录下的 resources/rag」这一档。
  const IndexResolution fallback =
      resolve_index_directory(std::filesystem::path("no/such/place"), {});
  if (std::filesystem::is_regular_file(source_index() / "chunks.jsonl")) {
    ASSERT_TRUE(fallback.found.has_value()) << fallback.error;
    EXPECT_NE(fallback.found->source, "命令行指定");
    EXPECT_EQ(fallback.found->directory, source_index());
  } else {
    // 索引还没生成时才会走到报错分支，那里必须把人试过的路径列出来。
    EXPECT_FALSE(fallback.found.has_value());
    EXPECT_NE(fallback.error.find("no/such/place"), std::string::npos);
    EXPECT_NE(fallback.error.find("build_index.py"), std::string::npos)
        << "报错要告诉人怎么生成索引";
  }
}

TEST(RagIndexSource, LinuxCollisionPagesKeepDistinctUrls) {
  // docs/APP.md 与 docs/app/index.md 在 Windows 文件系统上是同一个目录，
  // 但官网由 Linux 构建，两者是不同页面，URL 也必须不同。
  const std::optional<LexicalProvider> provider = LexicalProvider::load(source_index());
  if (!provider) {
    GTEST_SKIP() << "索引还没生成，先跑 scripts/rag/build_index.py";
  }
  for (const Chunk& chunk : provider->chunks()) {
    if (chunk.path == "APP.md") {
      EXPECT_NE(chunk.url.find("/APP/"), std::string::npos) << chunk.url;
    } else if (chunk.path == "app/index.md") {
      EXPECT_NE(chunk.url.find("/app/"), std::string::npos) << chunk.url;
    }
  }
}

class RagRealIndex : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::is_regular_file(source_index() / "chunks.jsonl")) {
      GTEST_SKIP() << "索引还没生成，先跑 scripts/rag/build_index.py";
    }
    std::string error;
    auto loaded = LexicalProvider::load(source_index(), &error);
    ASSERT_TRUE(loaded.has_value()) << error;
    provider_ = std::move(*loaded);
    std::string alias_error;
    aliases_ok_ = provider_.load_aliases(source_aliases(), &alias_error);
    ASSERT_TRUE(aliases_ok_) << alias_error;
  }

  LexicalProvider provider_;
  bool aliases_ok_ = false;
};

TEST_F(RagRealIndex, LoadsEveryChunkAndKeepsManifest) {
  EXPECT_GT(provider_.chunks().size(), 100u);
  EXPECT_FALSE(provider_.manifest_json().empty());
  EXPECT_NE(provider_.manifest_json().find("git_rev"), std::string::npos);
  EXPECT_FALSE(provider_.git_rev().empty());
  EXPECT_GT(provider_.alias_count(), 0u);
}

TEST_F(RagRealIndex, AnswersAQuestionAboutItsOwnCorpus) {
  Query query;
  query.text = "怎么建墙";
  query.k = 5;
  const std::vector<Chunk> results = provider_.search(query);
  ASSERT_FALSE(results.empty());
  std::size_t about_walls = 0;
  for (const Chunk& chunk : results) {
    if (chunk.title.find("墙") != std::string::npos ||
        chunk.text.find("墙") != std::string::npos) {
      ++about_walls;
    }
    EXPECT_FALSE(chunk.url.empty());
    EXPECT_FALSE(chunk.breadcrumb.empty());
    EXPECT_GT(chunk.score, 0.0);
  }
  EXPECT_GT(about_walls * 2, results.size())
      << "「怎么建墙」的多数结果该是讲墙的，而不是被「怎么」两个字带偏";
  // 分数必须单调不增，排序错了评测就不可复现。
  for (std::size_t i = 1; i < results.size(); ++i) {
    EXPECT_GE(results[i - 1].score, results[i].score);
  }
}

TEST_F(RagRealIndex, IdentifierQueryFindsTheApiPage) {
  Query query;
  query.text = "IHost";
  query.k = 5;
  const std::vector<Chunk> results = provider_.search(query);
  ASSERT_FALSE(results.empty());
  EXPECT_TRUE(paths_of(results, "plugin/api/host.md"))
      << "查标识符该直接命中插件 API 参考";
}

TEST_F(RagRealIndex, FiltersAreApplied) {
  Query query;
  query.text = "墙";
  query.k = 20;
  query.path_prefix = "plugin/";
  for (const Chunk& chunk : provider_.search(query)) {
    EXPECT_EQ(chunk.path.rfind("plugin/", 0), 0u) << chunk.path;
  }

  query.path_prefix.clear();
  query.lang = "zh";
  for (const Chunk& chunk : provider_.search(query)) {
    EXPECT_EQ(chunk.lang, "zh");
  }

  query.lang.clear();
  query.corpus = "nope";
  EXPECT_TRUE(provider_.search(query).empty()) << "未知语料该返回空，而不是全量";
}

TEST_F(RagRealIndex, ResultCountIsClamped) {
  Query query;
  query.text = "墙";
  query.k = 1000;
  EXPECT_LE(provider_.search(query).size(), static_cast<std::size_t>(kMaxResults));

  query.k = 0;
  EXPECT_LE(provider_.search(query).size(), 1u) << "k 下限收成 1，不是 0";
}

TEST_F(RagRealIndex, AliasExpansionPullsInCommandNames) {
  // 「圆角」不在文档里成词，靠别名表带出 fillet 才找得到。
  Query query;
  query.text = "圆角";
  query.k = 5;
  const std::vector<Chunk> with_aliases = provider_.search(query);

  LexicalProvider bare = provider_;
  bare.load_aliases(std::filesystem::path("no/such/aliases.json"));
  const std::vector<Chunk> without = bare.search(query);

  ASSERT_FALSE(with_aliases.empty());
  EXPECT_GE(with_aliases.size(), without.size());
}

}  // namespace
}  // namespace tamias::rag
