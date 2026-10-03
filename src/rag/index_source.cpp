#include "rag/index_source.h"

#include <cstdlib>
#include <utility>
#include <vector>

namespace tamias::rag {
namespace {

struct Candidate {
  std::filesystem::path path;
  std::string source;
};

const char* env_or_null(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' ? value : nullptr;
}

// 两个入口（索引目录 / 别名表）走同一套优先级，只是环境变量名与相对路径不同。
std::vector<Candidate> gather(const std::optional<std::filesystem::path>& requested,
                              const std::filesystem::path& application_dir,
                              const char* env_name, const std::filesystem::path& relative,
                              const char* requested_label) {
  std::vector<Candidate> candidates;
  if (requested.has_value() && !requested->empty()) {
    candidates.push_back(Candidate{*requested, requested_label});
  }
  if (const char* from_env = env_or_null(env_name); from_env != nullptr) {
    candidates.push_back(Candidate{from_env, std::string("环境变量 ") + env_name});
  }
  if (!application_dir.empty()) {
    candidates.push_back(
        Candidate{application_dir / relative, "可执行文件旁的 " + relative.generic_string()});
  }
#if defined(TAMIAS_SOURCE_DIR)
  {
    const std::filesystem::path source_root{TAMIAS_SOURCE_DIR};
    if (!source_root.empty()) {
      candidates.push_back(Candidate{source_root / relative,
                                     "源码目录下的 " + relative.generic_string()});
    }
  }
#endif
  return candidates;
}

}  // namespace

bool looks_like_index(const std::filesystem::path& directory) {
  std::error_code code;
  return std::filesystem::is_regular_file(directory / "chunks.jsonl", code);
}

IndexResolution resolve_index_directory(const std::optional<std::filesystem::path>& requested,
                                        const std::filesystem::path& application_dir) {
  const std::vector<Candidate> candidates =
      gather(requested, application_dir, "TAMIAS_RAG_INDEX",
             std::filesystem::path("resources") / "rag", "命令行指定");

  for (const Candidate& candidate : candidates) {
    if (looks_like_index(candidate.path)) {
      return IndexResolution{IndexLocation{candidate.path, candidate.source}, {}};
    }
  }

  std::string error = "找不到 RAG 索引（目录里要有 chunks.jsonl）。试过：";
  if (candidates.empty()) {
    error += "（没有任何候选路径）";
  }
  for (const Candidate& candidate : candidates) {
    error += "\n  - " + candidate.path.string() + "（" + candidate.source + "）";
  }
  error += "\n用 scripts/rag/build_index.py 生成，或用 --index=<目录> 指定。";
  return IndexResolution{std::nullopt, std::move(error)};
}

std::optional<std::filesystem::path> resolve_aliases_file(
    const std::optional<std::filesystem::path>& requested,
    const std::filesystem::path& application_dir) {
  const std::vector<Candidate> candidates =
      gather(requested, application_dir, "TAMIAS_RAG_ALIASES",
             std::filesystem::path("assets") / "rag" / "aliases.json", "命令行指定");

  std::error_code code;
  for (const Candidate& candidate : candidates) {
    if (std::filesystem::is_regular_file(candidate.path, code)) {
      return candidate.path;
    }
  }
  return std::nullopt;
}

}  // namespace tamias::rag
