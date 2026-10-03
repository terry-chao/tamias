#include "rag/bm25_index.h"

#include <algorithm>
#include <cmath>

namespace tamias::rag {
namespace {

constexpr double kK1 = 1.2;
constexpr double kB = 0.75;

}  // namespace

void Bm25Index::add(Field& field, std::uint32_t document, std::string_view text) {
  std::unordered_map<std::string, std::uint32_t> counts;
  for (const std::string& token : tokenize(text)) {
    ++counts[token];
  }
  std::uint32_t length = 0;
  for (const auto& [token, frequency] : counts) {
    field.postings[token].push_back(Posting{document, frequency});
    length += frequency;
  }
  field.lengths.resize(std::max<std::size_t>(field.lengths.size(), document + 1), 0);
  field.lengths[document] = length;
}

void Bm25Index::build(const std::vector<Chunk>& chunks) {
  title_ = Field{};
  breadcrumb_ = Field{};
  body_ = Field{};
  documents_ = chunks.size();

  for (std::size_t i = 0; i < chunks.size(); ++i) {
    const auto document = static_cast<std::uint32_t>(i);
    add(title_, document, chunks[i].title);
    add(breadcrumb_, document, chunks[i].breadcrumb);
    add(body_, document, chunks[i].text);
  }

  for (Field* field : {&title_, &breadcrumb_, &body_}) {
    field->lengths.resize(documents_, 0);
    std::uint64_t total = 0;
    for (const std::uint32_t length : field->lengths) {
      total += length;
    }
    field->average_length = documents_ == 0 ? 0.0 : static_cast<double>(total) / documents_;
  }
}

void Bm25Index::accumulate(const Field& field, double weight, std::string_view term,
                           std::vector<double>& scores) const {
  if (field.average_length <= 0.0) {
    return;
  }
  const auto found = field.postings.find(std::string(term));
  if (found == field.postings.end()) {
    return;
  }
  const auto document_frequency = static_cast<double>(found->second.size());
  const auto total = static_cast<double>(documents_);
  const double idf = std::log(1.0 + (total - document_frequency + 0.5) / (document_frequency + 0.5));

  for (const Posting& posting : found->second) {
    const double length = field.lengths[posting.document];
    const double norm = 1.0 - kB + kB * (length / field.average_length);
    const double tf = posting.frequency;
    scores[posting.document] +=
        weight * idf * (tf * (kK1 + 1.0)) / (tf + kK1 * norm);
  }
}

std::vector<double> Bm25Index::scores(const std::vector<Term>& query) const {
  std::vector<double> scores(documents_, 0.0);
  // 在这里再去一次重，而不是要求调用方先过 unique_terms：重复词会让同一篇
  // 被重复加分，是个很容易踩进去、又很难从结果上看出来的坑。
  for (const Term& term : unique_terms(query)) {
    accumulate(title_, Bm25Index::kTitleWeight * term.weight, term.text, scores);
    accumulate(breadcrumb_, Bm25Index::kBreadcrumbWeight * term.weight, term.text, scores);
    accumulate(body_, Bm25Index::kBodyWeight * term.weight, term.text, scores);
  }
  return scores;
}

}  // namespace tamias::rag
