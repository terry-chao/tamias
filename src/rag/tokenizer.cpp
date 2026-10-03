#include "rag/tokenizer.h"

#include <cctype>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace tamias::rag {
namespace {

// —— 极简 UTF-8 往返。只处理合法序列，坏字节当作 U+FFFD ——

constexpr std::uint32_t kReplacement = 0xFFFD;

std::uint32_t decode(std::string_view text, std::size_t& index) {
  const auto byte = [&text](std::size_t at) { return static_cast<unsigned char>(text[at]); };
  const std::uint32_t first = byte(index);
  if (first < 0x80) {
    ++index;
    return first;
  }
  int extra = 0;
  std::uint32_t code = 0;
  if ((first & 0xE0) == 0xC0) {
    extra = 1;
    code = first & 0x1Fu;
  } else if ((first & 0xF0) == 0xE0) {
    extra = 2;
    code = first & 0x0Fu;
  } else if ((first & 0xF8) == 0xF0) {
    extra = 3;
    code = first & 0x07u;
  } else {
    ++index;
    return kReplacement;
  }
  if (index + static_cast<std::size_t>(extra) >= text.size()) {
    index = text.size();
    return kReplacement;
  }
  for (int i = 0; i < extra; ++i) {
    const std::uint32_t next = byte(index + 1 + static_cast<std::size_t>(i));
    if ((next & 0xC0) != 0x80) {
      ++index;
      return kReplacement;
    }
    code = (code << 6) | (next & 0x3Fu);
  }
  index += static_cast<std::size_t>(extra) + 1;
  return code;
}

void append_utf8(std::string& out, std::uint32_t code) {
  if (code < 0x80) {
    out.push_back(static_cast<char>(code));
  } else if (code < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else if (code < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  }
}

// 汉字主体：基本区 + 扩展 A + 兼容区。扩展 B 及以上的生僻字这些文档里没有，
// 加进来只会让「按码点范围」这件事变复杂。
bool is_han(std::uint32_t code) {
  return (code >= 0x3400 && code <= 0x4DBF) || (code >= 0x4E00 && code <= 0x9FFF) ||
         (code >= 0xF900 && code <= 0xFAFF);
}

bool is_ident_byte(unsigned char byte) {
  return std::isalnum(byte) != 0 || byte == '_';
}

unsigned char lower(unsigned char byte) {
  return static_cast<unsigned char>(std::tolower(byte));
}

std::string to_lower(std::string_view text) {
  std::string out(text);
  for (char& ch : out) {
    ch = static_cast<char>(lower(static_cast<unsigned char>(ch)));
  }
  return out;
}

// 驼峰拆分。两种边界：小写→大写（`createWall`），以及大写串的结尾
// （`IHost` -> `I` + `Host`，`TopoDS` -> `Topo` + `DS`）。
std::vector<std::string> split_camel(std::string_view part) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 1; i < part.size(); ++i) {
    const unsigned char prev = static_cast<unsigned char>(part[i - 1]);
    const unsigned char cur = static_cast<unsigned char>(part[i]);
    const bool lower_to_upper = std::islower(prev) != 0 && std::isupper(cur) != 0;
    const bool acronym_end = std::isupper(prev) != 0 && std::isupper(cur) != 0 &&
                             i + 1 < part.size() &&
                             std::islower(static_cast<unsigned char>(part[i + 1])) != 0;
    if (lower_to_upper || acronym_end) {
      out.emplace_back(part.substr(start, i - start));
      start = i;
    }
  }
  out.emplace_back(part.substr(start));
  return out;
}

}  // namespace

std::string fold(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  std::size_t index = 0;
  while (index < text.size()) {
    std::uint32_t code = decode(text, index);
    if (code >= 0xFF01 && code <= 0xFF5E) {
      code -= 0xFEE0;  // 全角 ASCII
    } else if (code == 0x3000) {
      code = ' ';
    }
    append_utf8(out, code);
  }
  return out;
}

std::string normalize(std::string_view text) { return to_lower(fold(text)); }

std::vector<std::string> tokenize(std::string_view text) {
  // 在 fold 之后、转小写之前扫，驼峰边界才不会丢。
  const std::string folded = fold(text);
  std::vector<std::string> tokens;
  std::string ident;
  std::string han;

  const auto push = [&tokens](const std::string& token) {
    if (token.size() >= 1) {
      tokens.push_back(token);
    }
  };

  const auto flush_ident = [&] {
    if (ident.empty()) {
      return;
    }
    // 注意顺序：先按原样拆（驼峰信息只存在于原串里），最后才各自转小写。
    const std::string original = ident;
    ident.clear();
    // 计划里的标识符形状：必须字母或下划线开头。纯数字串（`0.5` 里的 `0`）
    // 不是标识符，当查询词也没什么用。
    const unsigned char first = static_cast<unsigned char>(original[0]);
    if (std::isalpha(first) == 0 && first != '_') {
      return;
    }
    const std::string whole = to_lower(original);
    push(whole);

    std::size_t start = 0;
    while (start <= original.size()) {
      const std::size_t end = original.find('_', start);
      const std::string_view part =
          std::string_view(original).substr(start, end == std::string::npos
                                                       ? std::string::npos
                                                       : end - start);
      // 下划线拆出的整段也算子词：`TopoDS_Shape` 要留 `topods`，
      // 否则有人直接打 `topods` 就找不到。
      const std::string lowered_part = to_lower(part);
      if (lowered_part.size() >= 2 && lowered_part != whole) {
        push(lowered_part);
      }
      for (const std::string& piece : split_camel(part)) {
        const std::string lowered = to_lower(piece);
        if (lowered.size() >= 2 && lowered != whole && lowered != lowered_part) {
          push(lowered);
        }
      }
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
  };

  const auto flush_han = [&] {
    if (han.empty()) {
      return;
    }
    // unigram + bigram 双写：不引分词器，也能兜住「建墙」这种两字词。
    std::size_t at = 0;
    std::size_t previous = 0;
    bool has_previous = false;
    while (at < han.size()) {
      std::size_t next = at + 1;
      while (next < han.size() && (static_cast<unsigned char>(han[next]) & 0xC0) == 0x80) {
        ++next;
      }
      const std::string glyph = han.substr(at, next - at);
      push(glyph);
      if (has_previous) {
        push(han.substr(previous, next - previous));
      }
      previous = at;
      has_previous = true;
      at = next;
    }
    han.clear();
  };

  std::size_t index = 0;
  while (index < folded.size()) {
    const std::size_t at = index;
    const std::uint32_t code = decode(folded, index);
    if (code < 0x80 && is_ident_byte(static_cast<unsigned char>(code))) {
      flush_han();
      ident.push_back(static_cast<char>(code));
    } else if (is_han(code)) {
      flush_ident();
      han.append(folded, at, index - at);
    } else {
      flush_ident();
      flush_han();
    }
  }
  flush_ident();
  flush_han();
  return tokens;
}

std::vector<Term> unique_terms(const std::vector<Term>& terms) {
  std::vector<Term> out;
  std::unordered_map<std::string, std::size_t> seen;
  for (const Term& term : terms) {
    if (term.text.empty() || term.weight <= 0.0) {
      continue;
    }
    const auto found = seen.find(term.text);
    if (found == seen.end()) {
      seen.emplace(term.text, out.size());
      out.push_back(term);
    } else if (term.weight > out[found->second].weight) {
      out[found->second].weight = term.weight;
    }
  }
  return out;
}

}  // namespace tamias::rag
