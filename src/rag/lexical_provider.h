#pragma once

#include "rag/bm25_index.h"
#include "rag/context_provider.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tamias::rag {

// 词法检索后端：内存 BM25，语料来自构建期产出的 chunks.jsonl。
// 零第三方依赖、断网可用；以后要语义检索就换 HttpProvider，接口不变。
class LexicalProvider final : public ContextProvider {
 public:
  // 载入失败返回 nullopt，原因写进 error —— 调用方要能原样报给人看。
  [[nodiscard]] static std::optional<LexicalProvider> load(
      const std::filesystem::path& directory, std::string* error = nullptr);

  // 别名表（口语 -> 规范词）。文件缺失只意味着少一层口语扩展，不算失败。
  // 返回是否载入成功，好让 CLI 能提示「别名表没读到」而不是静默降级。
  bool load_aliases(const std::filesystem::path& file, std::string* error = nullptr);

  [[nodiscard]] std::vector<Chunk> search(const Query& query) const override;
  [[nodiscard]] std::string manifest_json() const override { return manifest_; }

  [[nodiscard]] const std::vector<Chunk>& chunks() const { return chunks_; }
  [[nodiscard]] std::size_t alias_count() const { return aliases_.size(); }
  [[nodiscard]] const std::string& git_rev() const { return git_rev_; }

 private:
  // 查询期扩展用的词：正文 token 权重 1，别名带出来的规范词低一些。
  [[nodiscard]] std::vector<Term> query_terms(const std::string& text) const;

  std::vector<Chunk> chunks_;
  Bm25Index index_;
  std::string manifest_;
  std::string git_rev_;
  std::unordered_map<std::string, std::vector<std::string>> aliases_;
};

// 按 index_source 的优先级找到索引目录，整套载入（含别名表）。
// 装配处（McpService / AiPanel）共用，免得两边各写一遍「解析 + 载入 + 报错」。
// 别名表读不到只写进 alias_warning —— 少一层口语扩展，检索照跑。
[[nodiscard]] std::optional<LexicalProvider> load_from_application(
    const std::filesystem::path& application_dir, std::string* error = nullptr,
    std::string* alias_warning = nullptr);

}  // namespace tamias::rag
