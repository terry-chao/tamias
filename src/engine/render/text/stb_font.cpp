// stb_truetype 的实现只能出现一次：整个工程就这里定义 STB_TRUETYPE_IMPLEMENTATION。
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "engine/render/text/stb_font.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <utility>

namespace tamias {
namespace {

// 字号下限/上限：太小没法定格，太大一出错就是几百 MB。
constexpr float kMinPx = 1.f;
constexpr float kMaxPx = 512.f;

// TrueType name 表的记录号：1 = Font Family（stb 没给这几个号起名字）。
constexpr int kNameIdFontFamily = 1;

std::atomic<std::uint32_t> g_next_font_id{1};

// 字号 → stb 的缩放系数。px_size 是「像素字高（em）」，不是点大小。
float scale_for_px(const stbtt_fontinfo& info, float px_size) {
  return stbtt_ScaleForPixelHeight(&info, std::clamp(px_size, kMinPx, kMaxPx));
}

// 字体名是 UTF-16BE（Microsoft 平台记录）；这里只处理 BMP，够读家族名。
std::string utf16be_to_utf8(const unsigned char* data, int byte_length) {
  std::string out;
  out.reserve(static_cast<std::size_t>(byte_length) / 2);
  for (int i = 0; i + 1 < byte_length; i += 2) {
    const std::uint32_t code = (static_cast<std::uint32_t>(data[i]) << 8) |
                               static_cast<std::uint32_t>(data[i + 1]);
    if (code == 0) {
      continue;
    }
    if (code < 0x80u) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800u) {
      out.push_back(static_cast<char>(0xC0u | (code >> 6)));
      out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    } else {
      out.push_back(static_cast<char>(0xE0u | (code >> 12)));
      out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
      out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    }
  }
  return out;
}

}  // namespace

struct StbFont::Impl {
  std::vector<std::uint8_t> bytes;  // 必须常驻：stbtt_fontinfo 只持有指针
  stbtt_fontinfo info{};
  std::string family;
  std::uint32_t id = 0;
};

StbFont::StbFont(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

StbFont::~StbFont() = default;

Result<std::shared_ptr<StbFont>> StbFont::load_file(const std::filesystem::path& path,
                                                    int face_index) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return Err("stb_font: 打不开字体文件: " + path.string());
  }
  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  if (size <= 0) {
    return Err("stb_font: 字体文件是空的: " + path.string());
  }
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char*>(bytes.data()), size);
  if (file.gcount() != size) {
    return Err("stb_font: 读字体文件失败: " + path.string());
  }
  return from_bytes(std::move(bytes), face_index);
}

Result<std::shared_ptr<StbFont>> StbFont::from_bytes(std::vector<std::uint8_t> bytes,
                                                     int face_index) {
  if (bytes.empty()) {
    return Err("stb_font: 字体数据为空");
  }
  auto impl = std::make_unique<Impl>();
  impl->bytes = std::move(bytes);
  const int offset = stbtt_GetFontOffsetForIndex(impl->bytes.data(), face_index);
  if (offset < 0) {
    return Err("stb_font: 字体集合里没有第 " + std::to_string(face_index) + " 套字");
  }
  if (stbtt_InitFont(&impl->info, impl->bytes.data(), offset) == 0) {
    return Err("stb_font: 字体解析失败（不是有效的 TTF/OTF/TTC？）");
  }
  impl->id = g_next_font_id.fetch_add(1);

  int name_length = 0;
  // 名字按 UTF-16BE 字节返回（stb 只给 char*，字符类型无关紧要）。
  const unsigned char* name = reinterpret_cast<const unsigned char*>(stbtt_GetFontNameString(
      &impl->info, &name_length, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
      STBTT_MS_LANG_ENGLISH, kNameIdFontFamily));
  if (name != nullptr && name_length > 0) {
    impl->family = utf16be_to_utf8(name, name_length);
  }
  return std::shared_ptr<StbFont>(new StbFont(std::move(impl)));
}

GlyphMetrics StbFont::metrics(std::uint32_t codepoint, float px_size) const {
  GlyphMetrics out{};
  if (impl_ == nullptr) {
    return out;
  }
  const float scale = scale_for_px(impl_->info, px_size);
  const int codepoint_i = static_cast<int>(codepoint);
  int advance = 0;
  int left_side_bearing = 0;
  stbtt_GetCodepointHMetrics(&impl_->info, codepoint_i, &advance, &left_side_bearing);
  out.advance = static_cast<float>(advance) * scale;
  out.valid = stbtt_FindGlyphIndex(&impl_->info, codepoint_i) != 0;
  out.whitespace = codepoint == U' ';
  if (!out.valid) {
    return out;  // 缺字：只留 advance，消费方自己画占位
  }
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  stbtt_GetCodepointBitmapBox(&impl_->info, codepoint_i, scale, scale, &x0, &y0, &x1, &y1);
  out.bearing_x = static_cast<float>(x0);
  out.bearing_y = static_cast<float>(-y0);  // stb 的 y0 向下为正，这里翻成向上为正
  out.width = static_cast<float>(x1 - x0);
  out.height = static_cast<float>(y1 - y0);
  return out;
}

float StbFont::line_height(float px_size) const {
  if (impl_ == nullptr) {
    return 0.f;
  }
  const float scale = scale_for_px(impl_->info, px_size);
  int ascent = 0;
  int descent = 0;
  int line_gap = 0;
  stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &line_gap);
  return static_cast<float>(ascent - descent + line_gap) * scale;
}

float StbFont::ascent(float px_size) const {
  if (impl_ == nullptr) {
    return 0.f;
  }
  const float scale = scale_for_px(impl_->info, px_size);
  int ascent = 0;
  int descent = 0;
  int line_gap = 0;
  stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &line_gap);
  return static_cast<float>(ascent) * scale;
}

float StbFont::kerning(std::uint32_t left, std::uint32_t right, float px_size) const {
  if (impl_ == nullptr) {
    return 0.f;
  }
  const float scale = scale_for_px(impl_->info, px_size);
  return static_cast<float>(stbtt_GetCodepointKernAdvance(
             &impl_->info, static_cast<int>(left), static_cast<int>(right))) *
         scale;
}

GlyphBitmap StbFont::rasterize(std::uint32_t codepoint, float px_size) const {
  GlyphBitmap out{};
  if (impl_ == nullptr) {
    return out;
  }
  const float scale = scale_for_px(impl_->info, px_size);
  int width = 0;
  int height = 0;
  int x_offset = 0;
  int y_offset = 0;
  unsigned char* bitmap = stbtt_GetCodepointBitmap(&impl_->info, scale, scale,
                                                   static_cast<int>(codepoint), &width, &height,
                                                   &x_offset, &y_offset);
  if (bitmap != nullptr && width > 0 && height > 0) {
    out.width = width;
    out.height = height;
    out.bearing_x = static_cast<float>(x_offset);
    out.bearing_y = static_cast<float>(-y_offset);
    out.alpha.assign(bitmap, bitmap + static_cast<std::size_t>(width) * height);
  }
  if (bitmap != nullptr) {
    stbtt_FreeBitmap(bitmap, nullptr);
  }
  return out;
}

bool StbFont::has_glyph(std::uint32_t codepoint) const {
  if (impl_ == nullptr) {
    return false;
  }
  return stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(codepoint)) != 0;
}

std::uint32_t StbFont::id() const { return impl_ == nullptr ? 0 : impl_->id; }

const std::string& StbFont::family() const {
  static const std::string kEmpty;
  return impl_ == nullptr ? kEmpty : impl_->family;
}

}  // namespace tamias
