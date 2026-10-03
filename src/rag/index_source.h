#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace tamias::rag {

struct IndexLocation {
  std::filesystem::path directory;
  std::string source;  // 谁命中的，报错和日志里要能说清
};

struct IndexResolution {
  std::optional<IndexLocation> found;
  std::string error;  // 没找到时列出试过哪些路径
};

// 索引目录解析，优先级：
//   1. 显式给的路（`--index=` / 应用注入）
//   2. 环境变量 TAMIAS_RAG_INDEX
//   3. application_dir 旁的 resources/rag/
//   4. ${TAMIAS_SOURCE_DIR}/resources/rag/（开发机直接跑构建产物时用）
//
// application_dir 由调用方注入，src/rag 不自己去问系统要 exe 路径：那要么写平台
// API，要么依赖 tamias::base，两种都会把「只依赖标准库」这条不变量弄破。
// 应用侧传 exe 所在目录；命令行工具传当前工作目录。
[[nodiscard]] IndexResolution resolve_index_directory(
    const std::optional<std::filesystem::path>& requested,
    const std::filesystem::path& application_dir);

// 别名表位置，同样的优先级（TAMIAS_RAG_ALIASES）。这是个可选文件，
// 找不到就返回 nullopt，检索照跑，只是少一层口语扩展。
[[nodiscard]] std::optional<std::filesystem::path> resolve_aliases_file(
    const std::optional<std::filesystem::path>& requested,
    const std::filesystem::path& application_dir);

// 目录里有没有 chunks.jsonl —— 判断一个候选是不是真的能用的索引目录。
[[nodiscard]] bool looks_like_index(const std::filesystem::path& directory);

}  // namespace tamias::rag
