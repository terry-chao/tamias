#include "rag/tokenizer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace tamias::rag {
namespace {

bool has(const std::vector<std::string>& tokens, const std::string& token) {
  return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
}

TEST(RagTokenizer, KeepsIdentifierWholeAndEmitsSubwords) {
  const std::vector<std::string> tokens = tokenize("tamias_create_wall");
  // 整体保留：查询里直接打全名也得命中。
  EXPECT_TRUE(has(tokens, "tamias_create_wall"));
  // 子词补发：否则查 `wall` 命中不了 `create_wall`。
  EXPECT_TRUE(has(tokens, "create"));
  EXPECT_TRUE(has(tokens, "wall"));
}

TEST(RagTokenizer, SplitsCamelCase) {
  const std::vector<std::string> tokens = tokenize("IHost.Features");
  EXPECT_TRUE(has(tokens, "ihost"));
  EXPECT_TRUE(has(tokens, "features"));
  EXPECT_TRUE(has(tokens, "host")) << "IHost 要能拆出 host";

  const std::vector<std::string> acronym = tokenize("TopoDS_Shape");
  EXPECT_TRUE(has(acronym, "topods"));
  EXPECT_TRUE(has(acronym, "shape"));
  EXPECT_TRUE(has(acronym, "topo")) << "驼峰串结尾 `DS` 之前的 `Topo` 要留下";
  EXPECT_TRUE(has(acronym, "ds"));
}

TEST(RagTokenizer, CjkUnigramAndBigram) {
  const std::vector<std::string> tokens = tokenize("建墙");
  EXPECT_TRUE(has(tokens, "建"));
  EXPECT_TRUE(has(tokens, "墙"));
  EXPECT_TRUE(has(tokens, "建墙"));
}

TEST(RagTokenizer, CjkRunBrokenByPunctuation) {
  // 标点是分隔符，不该把两个句子串成一个 bigram。
  const std::vector<std::string> tokens = tokenize("建墙。删梁");
  EXPECT_TRUE(has(tokens, "建墙"));
  EXPECT_TRUE(has(tokens, "删梁"));
  EXPECT_FALSE(has(tokens, "墙删"));
  EXPECT_FALSE(has(tokens, "。"));
}

TEST(RagTokenizer, FoldsFullWidthAndLowercases) {
  EXPECT_EQ(normalize("ＡＢＣ１２３"), "abc123");
  EXPECT_EQ(normalize("Ｗａｌｌ"), "wall");
  // 全角空格也是空格：它是分隔符，不是汉字。
  const std::vector<std::string> tokens = tokenize("建　墙");
  EXPECT_FALSE(has(tokens, "建墙")) << "全角空格两侧不该连成 bigram";
  EXPECT_TRUE(has(tokens, "建"));
  EXPECT_TRUE(has(tokens, "墙"));
}

TEST(RagTokenizer, FoldKeepsCaseSoCamelSurvives) {
  // fold 不转小写，tokenize 才拆得动驼峰；normalize 才是 fold + 小写。
  EXPECT_EQ(fold("TopoDS"), "TopoDS");
  EXPECT_EQ(normalize("TopoDS"), "topods");
}

TEST(RagTokenizer, PureDigitRunsAreNotIdentifiers) {
  const std::vector<std::string> tokens = tokenize("0.5 人日");
  EXPECT_FALSE(has(tokens, "0"));
  EXPECT_FALSE(has(tokens, "5"));
  EXPECT_TRUE(has(tokens, "人日"));
}

TEST(RagTokenizer, UniqueTermsKeepsHighestWeight) {
  const std::vector<Term> terms = {{"wall", 1.0}, {"beam", 0.5}, {"wall", 0.5}};
  const std::vector<Term> unique = unique_terms(terms);
  ASSERT_EQ(unique.size(), 2u);
  EXPECT_EQ(unique[0].text, "wall");
  EXPECT_DOUBLE_EQ(unique[0].weight, 1.0);
  EXPECT_EQ(unique[1].text, "beam");
}

TEST(RagTokenizer, EmptyInputIsHarmless) {
  EXPECT_TRUE(tokenize("").empty());
  EXPECT_TRUE(tokenize("   \n\t").empty());
  EXPECT_EQ(normalize(""), "");
}

}  // namespace
}  // namespace tamias::rag
