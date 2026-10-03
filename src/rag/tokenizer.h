#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tamias::rag {

// 查询词 + 权重。别名扩展出来的规范词权重低一些（PLAN-RAG §5「查询期扩展」）。
struct Term {
  std::string text;
  double weight = 1.0;
};

// 宽度折叠：全角 ASCII -> 半角、全角空格 -> 空格。**不动大小写**，
// 因为标识符的驼峰拆分要靠原始大小写（`TopoDS` -> `topo` + `ds`）。
[[nodiscard]] std::string fold(std::string_view text);

// fold + ASCII 小写。语料与查询两边都走它，保证落进同一个词表。
[[nodiscard]] std::string normalize(std::string_view text);

// 分词：
//   - ASCII 标识符 `[A-Za-z0-9_]+` 整体一个 token，**同时**补发按 `_` 与驼峰
//     拆出的子词——否则查 `wall` 命中不了 `create_wall`；
//   - 连续汉字发 unigram + bigram，不引分词器依赖。
// 返回的 token 已是小写。
[[nodiscard]] std::vector<std::string> tokenize(std::string_view text);

// 同一批词里重复没有意义（BM25 里 tf 是文档侧的），去重并保留最大权重。
[[nodiscard]] std::vector<Term> unique_terms(const std::vector<Term>& terms);

}  // namespace tamias::rag
