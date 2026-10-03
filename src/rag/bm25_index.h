#pragma once

#include "rag/context_provider.h"
#include "rag/tokenizer.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tamias::rag {

// 字段加权 BM25，k1 = 1.2、b = 0.75：
//
//   score = 3·BM25(title) + 2·BM25(breadcrumb) + 1·BM25(正文)
//
// 三个字段各自维护倒排表、文档长度与平均长度。**不**用「把标题的 token 重复
// 三遍拼进正文」来实现权重：在几百字的正文里那点 tf 几乎不起作用，而且会把
// 文档长度归一化一起带偏（PLAN-RAG §5）。
class Bm25Index {
 public:
  static constexpr double kTitleWeight = 3.0;
  static constexpr double kBreadcrumbWeight = 2.0;
  static constexpr double kBodyWeight = 1.0;

  void build(const std::vector<Chunk>& chunks);
  // 每个 chunk 的得分，下标与 build 时的 chunks 一一对应。
  [[nodiscard]] std::vector<double> scores(const std::vector<Term>& query) const;
  [[nodiscard]] std::size_t documents() const { return documents_; }
  [[nodiscard]] bool empty() const { return documents_ == 0; }

 private:
  struct Posting {
    std::uint32_t document = 0;
    std::uint32_t frequency = 0;
  };
  struct Field {
    std::unordered_map<std::string, std::vector<Posting>> postings;
    std::vector<std::uint32_t> lengths;
    double average_length = 0.0;
  };

  void add(Field& field, std::uint32_t document, std::string_view text);
  void accumulate(const Field& field, double weight, std::string_view term,
                  std::vector<double>& scores) const;

  std::size_t documents_ = 0;
  Field title_;
  Field breadcrumb_;
  Field body_;
};

}  // namespace tamias::rag
