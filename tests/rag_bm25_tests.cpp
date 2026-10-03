#include "rag/bm25_index.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace tamias::rag {
namespace {

std::vector<Term> terms_of(const std::string& text) {
  std::vector<Term> terms;
  for (const std::string& token : tokenize(text)) {
    terms.push_back(Term{token, 1.0});
  }
  return unique_terms(terms);
}

TEST(RagBm25, TitleOutweighsBody) {
  const std::vector<Chunk> chunks = {
      Chunk{.id = "title", .title = "wall"},
      Chunk{.id = "body", .text = "wall"},
  };
  Bm25Index index;
  index.build(chunks);
  const std::vector<double> scores = index.scores(terms_of("wall"));

  ASSERT_EQ(scores.size(), 2u);
  EXPECT_GT(scores[0], scores[1]) << "标题命中要压过正文命中（权重 3:1）";
  EXPECT_GT(scores[1], 0.0);
}

TEST(RagBm25, BreadcrumbSitsBetweenTitleAndBody) {
  const std::vector<Chunk> chunks = {
      Chunk{.id = "title", .title = "wall"},
      Chunk{.id = "crumb", .breadcrumb = "wall"},
      Chunk{.id = "body", .text = "wall"},
  };
  Bm25Index index;
  index.build(chunks);
  const std::vector<double> scores = index.scores(terms_of("wall"));
  EXPECT_GT(scores[0], scores[1]);
  EXPECT_GT(scores[1], scores[2]);
}

TEST(RagBm25, LongDocumentsArePenalised) {
  std::string long_text = "wall";
  for (int i = 0; i < 60; ++i) {
    long_text += " filler";
  }
  const std::vector<Chunk> chunks = {
      Chunk{.id = "short", .text = "wall"},
      Chunk{.id = "long", .text = long_text},
  };
  Bm25Index index;
  index.build(chunks);
  const std::vector<double> scores = index.scores(terms_of("wall"));
  ASSERT_EQ(scores.size(), 2u);
  EXPECT_GT(scores[0], scores[1]) << "同样一次命中，短文档该排在前面";
}

TEST(RagBm25, RepeatedTermsDoNotDoubleCountDocuments) {
  // 同一篇里 tf 高是应该加分的，但重复查询词不能重复累加。
  const std::vector<Chunk> chunks = {
      Chunk{.id = "twice", .title = "wall wall"},
      Chunk{.id = "once", .title = "wall"},
  };
  Bm25Index index;
  index.build(chunks);
  const std::vector<double> once = index.scores(terms_of("wall"));
  const std::vector<double> doubled = index.scores({{"wall", 1.0}, {"wall", 1.0},
                                                    {"wall", 1.0}});
  EXPECT_DOUBLE_EQ(once[0], doubled[0]) << "unique_terms 之外没有去重，这里守住";
}

TEST(RagBm25, UnmatchedQueryScoresNothing) {
  const std::vector<Chunk> chunks = {Chunk{.id = "a", .title = "wall"}};
  Bm25Index index;
  index.build(chunks);
  const std::vector<double> scores = index.scores(terms_of("beam"));
  ASSERT_EQ(scores.size(), 1u);
  EXPECT_DOUBLE_EQ(scores[0], 0.0);
}

TEST(RagBm25, EmptyInputsAreHarmless) {
  Bm25Index index;
  index.build({});
  EXPECT_TRUE(index.empty());
  EXPECT_EQ(index.documents(), 0u);
  EXPECT_TRUE(index.scores(terms_of("wall")).empty());

  index.build({Chunk{.id = "a", .title = "wall"}});
  EXPECT_FALSE(index.empty());
  const std::vector<double> scores = index.scores({});
  ASSERT_EQ(scores.size(), 1u);
  EXPECT_DOUBLE_EQ(scores[0], 0.0);
}

TEST(RagBm25, ChunkWithoutAnyFieldIsSkippedCleanly) {
  const std::vector<Chunk> chunks = {Chunk{.id = "empty"}, Chunk{.id = "real", .title = "wall"}};
  Bm25Index index;
  index.build(chunks);
  const std::vector<double> scores = index.scores(terms_of("wall"));
  ASSERT_EQ(scores.size(), 2u);
  EXPECT_DOUBLE_EQ(scores[0], 0.0);
  EXPECT_GT(scores[1], 0.0);
}

}  // namespace
}  // namespace tamias::rag
