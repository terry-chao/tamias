// tamias_rag_cli —— 无 Qt 的检索命令行入口，CI 与手工调试共用。
//
//   tamias_rag_cli --index=resources/rag --query="怎么建墙" --k=5
//
// 存在的理由：金标集必须跑**真代码**。评测若另写一份 Python 分词，测过的和发布
// 的就不是同一个东西（PLAN-RAG §9）。所以这个入口不引 Qt，也不引 engine。

#include "mcp/json.h"
#include "rag/index_source.h"
#include "rag/lexical_provider.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

struct Options {
  std::optional<std::filesystem::path> index;
  std::optional<std::filesystem::path> aliases;
  std::string query;
  std::string corpus;
  std::string lang;
  std::string path_prefix;
  int k = 5;
  bool want_manifest = false;
  int bench = 0;  // > 0 = 只测速，跑这么多遍查询
};

void print_usage() {
  std::cerr << "用法：tamias_rag_cli [选项]\n"
               "  --index=<目录>        索引目录（默认按 TAMIAS_RAG_INDEX / 可执行文件旁 / 源码目录找）\n"
               "  --query=<文本>        查询词\n"
               "  --k=<n>               返回条数，1–20，默认 5\n"
               "  --corpus=<名字>       限定语料，空 = 全部\n"
               "  --lang=<zh|en>        限定语言\n"
               "  --path-prefix=<前缀>  限定文档路径前缀\n"
               "  --aliases=<文件>      口语别名表，默认 assets/rag/aliases.json\n"
               "  --manifest            只打印语料清单（不查询）\n"
               "结果是一段 JSON 数组，字段与 chunks.jsonl 对应，另有 score。\n";
}

// 支持 `--k=5` 与 `--manifest` 两种形状。位置参数当查询词，省得每次都打 --query。
std::optional<Options> parse_args(const std::vector<std::string>& args, std::string* error) {
  Options options;
  for (const std::string& argument : args) {
    if (argument == "--help" || argument == "-h") {
      print_usage();
      std::exit(0);
    }
    if (argument == "--manifest") {
      options.want_manifest = true;
      continue;
    }
    if (argument.starts_with("--bench")) {
      // --bench 或 --bench=200
      const std::size_t equals_bench = argument.find('=');
      options.bench = 200;
      if (equals_bench != std::string::npos) {
        try {
          options.bench = std::stoi(argument.substr(equals_bench + 1));
        } catch (const std::exception&) {
          *error = "--bench 需要整数遍数，例如 --bench=200";
          return std::nullopt;
        }
      }
      if (options.bench <= 0) {
        *error = "--bench 的遍数要大于 0";
        return std::nullopt;
      }
      continue;
    }
    const std::size_t equals = argument.find('=');
    const std::string name = argument.substr(0, equals);
    const std::string value =
        equals == std::string::npos ? std::string() : argument.substr(equals + 1);

    if (equals == std::string::npos && !argument.starts_with("--")) {
      if (!options.query.empty()) {
        *error = "给了多个查询词，用 --query= 明确指定";
        return std::nullopt;
      }
      options.query = argument;
      continue;
    }
    if (name == "--index") {
      options.index = value;
    } else if (name == "--aliases") {
      options.aliases = value;
    } else if (name == "--query") {
      options.query = value;
    } else if (name == "--corpus") {
      options.corpus = value;
    } else if (name == "--lang") {
      options.lang = value;
    } else if (name == "--path-prefix") {
      options.path_prefix = value;
    } else if (name == "--k") {
      try {
        options.k = std::stoi(value);
      } catch (const std::exception&) {
        *error = "--k 需要一个整数，收到 " + value;
        return std::nullopt;
      }
    } else {
      *error = "不认识的参数 " + argument;
      return std::nullopt;
    }
  }
  return options;
}

tamias::mcp::Json to_json(const tamias::rag::Chunk& chunk) {
  return tamias::mcp::Json::object({
      {"id", tamias::mcp::Json::string(chunk.id)},
      {"corpus", tamias::mcp::Json::string(chunk.corpus)},
      {"path", tamias::mcp::Json::string(chunk.path)},
      {"anchor", tamias::mcp::Json::string(chunk.anchor)},
      {"title", tamias::mcp::Json::string(chunk.title)},
      {"breadcrumb", tamias::mcp::Json::string(chunk.breadcrumb)},
      {"lang", tamias::mcp::Json::string(chunk.lang)},
      {"url", tamias::mcp::Json::string(chunk.url)},
      {"score", tamias::mcp::Json::number(chunk.score)},
      {"text", tamias::mcp::Json::string(chunk.text)},
  });
}

int run(const std::vector<std::string>& args) {
  std::string error;
  const std::optional<Options> parsed = parse_args(args, &error);
  if (!parsed) {
    std::cerr << error << "\n\n";
    print_usage();
    return 2;
  }
  const Options& options = *parsed;

  // 命令行工具用当前目录代替「可执行文件旁」——在仓库根下直接跑最顺手。
  const std::filesystem::path application_dir = std::filesystem::current_path();
  const tamias::rag::IndexResolution resolved =
      tamias::rag::resolve_index_directory(options.index, application_dir);
  if (!resolved.found) {
    std::cerr << resolved.error << "\n";
    return 1;
  }

  const auto load_started = std::chrono::steady_clock::now();
  const std::optional<tamias::rag::LexicalProvider> provider =
      tamias::rag::LexicalProvider::load(resolved.found->directory, &error);
  const double load_ms = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - load_started)
                             .count();
  if (!provider) {
    std::cerr << error << "\n";
    return 1;
  }

  if (options.want_manifest) {
    std::cout << provider->manifest_json() << "\n";
    return 0;
  }

  if (options.query.empty()) {
    std::cerr << "没有查询词。用 --query=<文本> 或直接给一个位置参数。\n";
    return 2;
  }

  tamias::rag::LexicalProvider loaded = *provider;
  const std::optional<std::filesystem::path> aliases =
      tamias::rag::resolve_aliases_file(options.aliases, application_dir);
  if (aliases.has_value()) {
    std::string alias_error;
    if (!loaded.load_aliases(*aliases, &alias_error)) {
      std::cerr << "别名表没读到（不影响检索）：" << alias_error << "\n";
    }
  }

  tamias::rag::Query query;
  query.text = options.query;
  query.corpus = options.corpus;
  query.lang = options.lang;
  query.path_prefix = options.path_prefix;
  query.k = options.k;

  if (options.bench > 0) {
    // M1 的验收指标：载入 < 100 ms、查询 P95 < 30 ms。测出来给 CI 和人看。
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(options.bench));
    std::size_t hits = 0;
    for (int i = 0; i < options.bench; ++i) {
      const auto started = std::chrono::steady_clock::now();
      const std::vector<tamias::rag::Chunk> found = loaded.search(query);
      samples.push_back(
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
              .count());
      hits += found.size();
    }
    std::sort(samples.begin(), samples.end());
    const auto at = [&samples](double fraction) {
      return samples[static_cast<std::size_t>(fraction * (samples.size() - 1))];
    };
    std::cerr << "载入 " << load_ms << " ms（" << loaded.chunks().size() << " 块，"
              << loaded.alias_count() << " 条别名）\n"
              << "查询 " << options.bench << " 遍：P50 " << at(0.50) << " ms / P95 "
              << at(0.95) << " ms / max " << samples.back() << " ms；平均命中 "
              << (static_cast<double>(hits) / options.bench) << " 条\n";
    return 0;
  }

  tamias::mcp::Json results = tamias::mcp::Json::array();
  for (const tamias::rag::Chunk& chunk : loaded.search(query)) {
    results.push_back(to_json(chunk));
  }
  std::cout << results.dump() << "\n";
  return 0;
}

}  // namespace

#if defined(_WIN32)
namespace {

std::string wide_to_utf8(const wchar_t* text) {
  if (text == nullptr) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return {};
  }
  std::string out(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  return out;
}

}  // namespace

// Windows 上 `main(char** argv)` 拿到的是 ANSI 代码页（中文系统 = GBK），
// 中文查询词在那之前就已经坏了。所以忽略 argv，直接读宽字符命令行。
// 用 main 而不是 wmain 是因为 MSVC 要额外配 /ENTRY:wmainCRTStartup 才认 wmain。
int main(int, char**) {
  int count = 0;
  wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
  std::vector<std::string> args;
  if (wide != nullptr) {
    args.reserve(static_cast<std::size_t>(count));
    for (int i = 1; i < count; ++i) {
      args.push_back(wide_to_utf8(wide[i]));
    }
    LocalFree(wide);
  }
  return run(args);
}

#else

int main(int argc, char** argv) {
  return run(std::vector<std::string>(argv + 1, argv + argc));
}

#endif
