#include "engine/render/text/text_utf8.h"

#include <cstdint>

namespace tamias {
namespace {

constexpr char32_t kReplacement = 0xFFFD;
// 超过 U+10FFFF 的编码：UTF-8 最多 4 字节。
constexpr std::uint32_t kMaxCodepoint = 0x10FFFF;

// 判断续字节（10xxxxxx）并取低 6 位；不是续字节返回 -1。
int continuation_byte(char byte) {
  const auto value = static_cast<unsigned char>(byte);
  if ((value & 0xC0u) != 0x80u) {
    return -1;
  }
  return static_cast<int>(value & 0x3Fu);
}

}  // namespace

std::u32string decode_utf8(std::string_view text) {
  std::u32string out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i]);
    if (lead < 0x80u) {
      out.push_back(static_cast<char32_t>(lead));
      ++i;
      continue;
    }
    int extra = 0;
    std::uint32_t codepoint = 0;
    std::uint32_t minimum = 0;
    if ((lead & 0xE0u) == 0xC0u) {
      extra = 1;
      codepoint = lead & 0x1Fu;
      minimum = 0x80u;
    } else if ((lead & 0xF0u) == 0xE0u) {
      extra = 2;
      codepoint = lead & 0x0Fu;
      minimum = 0x800u;
    } else if ((lead & 0xF8u) == 0xF0u) {
      extra = 3;
      codepoint = lead & 0x07u;
      minimum = 0x10000u;
    } else {
      // 0x80..0xBF：孤立的续字节；0xF8..0xFF：非法首字节。
      out.push_back(kReplacement);
      ++i;
      continue;
    }
    if (i + static_cast<std::size_t>(extra) >= text.size()) {
      out.push_back(kReplacement);  // 截断序列
      ++i;
      continue;
    }
    bool ok = true;
    for (int k = 1; k <= extra; ++k) {
      const int low6 = continuation_byte(text[i + static_cast<std::size_t>(k)]);
      if (low6 < 0) {
        ok = false;
        break;
      }
      codepoint = (codepoint << 6) | static_cast<std::uint32_t>(low6);
    }
    if (!ok || codepoint < minimum || codepoint > kMaxCodepoint ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
      out.push_back(kReplacement);
      // 结构完整：整段吞掉，脏序列只换来一个替换符；结构不完整：只吞首字节。
      i += ok ? static_cast<std::size_t>(extra) + 1 : 1;
      continue;
    }
    out.push_back(static_cast<char32_t>(codepoint));
    i += static_cast<std::size_t>(extra) + 1;
  }
  return out;
}

}  // namespace tamias
