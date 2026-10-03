#include "rag/lexical_provider.h"

#include "mcp/json.h"
#include "rag/index_source.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace tamias::rag {
namespace {

// 别名扩展出来的规范词权重低于用户原话：它是「猜的」，不该盖过原话。
constexpr double kAliasWeight = 0.5;

std::string field_of(const mcp::Json& object, std::string_view key) {
  const mcp::Json* value = object.find(key);
  return value != nullptr && value->is_string() ? value->as_string() : std::string();
}

void read_strings(const mcp::Json& value, std::vector<std::string>& out) {
  if (value.is_string()) {
    out.push_back(value.as_string());
  } else if (value.is_array()) {
    for (const mcp::Json& item : value.items()) {
      if (item.is_string()) {
        out.push_back(item.as_string());
      }
    }
  }
}

}  // namespace

std::optional<LexicalProvider> LexicalProvider::load(const std::filesystem::path& directory,
                                                     std::string* error) {
  const std::filesystem::path chunks_path = directory / "chunks.jsonl";
  std::ifstream stream(chunks_path, std::ios::binary);
  if (!stream) {
    if (error != nullptr) {
      *error = "打不开索引文件 " + chunks_path.string();
    }
    return std::nullopt;
  }

  LexicalProvider provider;
  std::string line;
  std::size_t number = 0;
  while (std::getline(stream, line)) {
    ++number;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    std::string parse_error;
    const std::optional<mcp::Json> parsed = mcp::Json::parse(line, &parse_error);
    if (!parsed || !parsed->is_object()) {
      if (error != nullptr) {
        *error = chunks_path.string() + " 第 " + std::to_string(number) +
                 " 行不是合法的 JSON 对象：" + parse_error;
      }
      return std::nullopt;
    }
    Chunk chunk;
    chunk.id = field_of(*parsed, "id");
    chunk.corpus = field_of(*parsed, "corpus");
    chunk.path = field_of(*parsed, "path");
    chunk.anchor = field_of(*parsed, "anchor");
    chunk.title = field_of(*parsed, "title");
    chunk.breadcrumb = field_of(*parsed, "breadcrumb");
    chunk.lang = field_of(*parsed, "lang");
    chunk.url = field_of(*parsed, "url");
    chunk.text = field_of(*parsed, "text");
    if (chunk.id.empty()) {
      if (error != nullptr) {
        *error = chunks_path.string() + " 第 " + std::to_string(number) + " 行缺少 id";
      }
      return std::nullopt;
    }
    provider.chunks_.push_back(std::move(chunk));
  }

  if (provider.chunks_.empty()) {
    if (error != nullptr) {
      *error = chunks_path.string() + " 里一条 chunk 都没有";
    }
    return std::nullopt;
  }

  const std::filesystem::path manifest_path = directory / "manifest.json";
  std::ifstream manifest_stream(manifest_path, std::ios::binary);
  if (manifest_stream) {
    std::ostringstream buffer;
    buffer << manifest_stream.rdbuf();
    provider.manifest_ = buffer.str();
    if (const std::optional<mcp::Json> manifest = mcp::Json::parse(provider.manifest_)) {
      provider.git_rev_ = field_of(*manifest, "git_rev");
    }
  }

  provider.index_.build(provider.chunks_);
  return provider;
}

bool LexicalProvider::load_aliases(const std::filesystem::path& file, std::string* error) {
  aliases_.clear();
  std::ifstream stream(file, std::ios::binary);
  if (!stream) {
    if (error != nullptr) {
      *error = "打不开别名表 " + file.string();
    }
    return false;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  const std::optional<mcp::Json> parsed = mcp::Json::parse(buffer.str(), error);
  if (!parsed || !parsed->is_object()) {
    if (error != nullptr && !error->empty()) {
      *error = file.string() + " 不是合法的 JSON 对象：" + *error;
    }
    return false;
  }
  for (const auto& [spoken, canonical] : parsed->members()) {
    if (spoken.empty() || spoken.front() == '$') {
      continue;  // $ 开头的是给人看的说明，不是别名
    }
    std::vector<std::string> words;
    read_strings(canonical, words);
    if (!words.empty()) {
      aliases_.emplace(normalize(spoken), std::move(words));
    }
  }
  return true;
}

std::vector<Term> LexicalProvider::query_terms(const std::string& text) const {
  std::vector<Term> terms;
  for (const std::string& token : tokenize(text)) {
    terms.push_back(Term{token, 1.0});
  }
  if (!aliases_.empty()) {
    const std::string normalized = normalize(text);
    for (const auto& [spoken, canonical] : aliases_) {
      if (normalized.find(spoken) == std::string::npos) {
        continue;
      }
      for (const std::string& word : canonical) {
        for (const std::string& token : tokenize(word)) {
          terms.push_back(Term{token, kAliasWeight});
        }
      }
    }
  }
  return unique_terms(terms);
}

std::optional<LexicalProvider> load_from_application(const std::filesystem::path& application_dir,
                                                     std::string* error,
                                                     std::string* alias_warning) {
  const IndexResolution resolved = resolve_index_directory(std::nullopt, application_dir);
  if (!resolved.found) {
    if (error != nullptr) {
      *error = resolved.error;
    }
    return std::nullopt;
  }
  std::optional<LexicalProvider> provider = LexicalProvider::load(resolved.found->directory, error);
  if (!provider) {
    return std::nullopt;
  }
  if (const std::optional<std::filesystem::path> aliases =
          resolve_aliases_file(std::nullopt, application_dir);
      aliases.has_value()) {
    std::string alias_error;
    if (!provider->load_aliases(*aliases, &alias_error) && alias_warning != nullptr) {
      *alias_warning = alias_error;
    }
  }
  return provider;
}

std::vector<Chunk> LexicalProvider::search(const Query& query) const {
  const std::vector<Term> terms = query_terms(query.text);
  const int wanted = std::clamp(query.k, 1, kMaxResults);
  std::vector<Chunk> results;
  if (terms.empty() || index_.empty()) {
    return results;
  }

  const std::vector<double> scores = index_.scores(terms);
  std::vector<std::size_t> order;
  for (std::size_t i = 0; i < scores.size(); ++i) {
    if (scores[i] <= 0.0) {
      continue;
    }
    const Chunk& chunk = chunks_[i];
    if (!query.corpus.empty() && chunk.corpus != query.corpus) {
      continue;
    }
    if (!query.lang.empty() && chunk.lang != query.lang) {
      continue;
    }
    if (!query.path_prefix.empty() && chunk.path.rfind(query.path_prefix, 0) != 0) {
      continue;
    }
    order.push_back(i);
  }

  // 同分时按 id 排，保证同样的查询永远给同样的顺序（评测要可复现）。
  std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
    if (scores[left] != scores[right]) {
      return scores[left] > scores[right];
    }
    return chunks_[left].id < chunks_[right].id;
  });

  if (order.size() > static_cast<std::size_t>(wanted)) {
    order.resize(static_cast<std::size_t>(wanted));
  }
  for (const std::size_t index : order) {
    Chunk chunk = chunks_[index];
    chunk.score = scores[index];
    results.push_back(std::move(chunk));
  }
  return results;
}

}  // namespace tamias::rag
