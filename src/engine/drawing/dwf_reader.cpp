#include "engine/drawing/dwf_reader.h"

#include "engine/drawing/zip_archive.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string_view>

namespace tamias {
namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- 小工具

std::string to_lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

bool ends_with_ci(std::string_view text, std::string_view suffix) {
  if (text.size() < suffix.size()) {
    return false;
  }
  return to_lower(text.substr(text.size() - suffix.size())) == to_lower(suffix);
}

void decode_entities(std::string& text) {
  struct Pair {
    const char* from;
    const char* to;
  };
  static constexpr Pair kPairs[] = {{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
                                    {"&apos;", "'"}, {"&amp;", "&"}};
  for (const Pair& pair : kPairs) {
    std::size_t pos = 0;
    const std::string from(pair.from);
    while ((pos = text.find(from, pos)) != std::string::npos) {
      text.replace(pos, from.size(), pair.to);
      pos += std::char_traits<char>::length(pair.to);
    }
  }
}

// 取一个属性（属性名区分大小写，XPS 里就写成这样）。
struct XmlAttr {
  std::string name;
  std::string value;
};

struct XmlTag {
  std::string name;
  std::vector<XmlAttr> attrs;
  bool closing = false;
  bool self_closing = false;

  [[nodiscard]] const std::string* attr(std::string_view key) const {
    for (const XmlAttr& a : attrs) {
      if (a.name == key) {
        return &a.value;
      }
    }
    return nullptr;
  }

  [[nodiscard]] double number(std::string_view key, double fallback) const {
    const std::string* text = attr(key);
    if (text == nullptr || text->empty()) {
      return fallback;
    }
    char* end = nullptr;
    const double value = std::strtod(text->c_str(), &end);
    return end != text->c_str() ? value : fallback;
  }
};

// 极简 XML 扫描：XPS 的 FixedPage 只用到元素 + 属性，够用就行；不建树。
// 注释 / 处理指令 / DOCTYPE 直接跳过；CDATA 当文本跳过。
bool next_tag(std::string_view xml, std::size_t& cursor, XmlTag& tag) {
  while (cursor < xml.size()) {
    const std::size_t open = xml.find('<', cursor);
    if (open == std::string_view::npos) {
      return false;
    }
    cursor = open + 1;
    if (cursor >= xml.size()) {
      return false;
    }
    if (xml[cursor] == '!' || xml[cursor] == '?') {
      const std::size_t close = xml.find('>', cursor);
      if (close == std::string_view::npos) {
        return false;
      }
      cursor = close + 1;
      continue;
    }
    break;
  }
  if (cursor >= xml.size()) {
    return false;
  }

  tag = XmlTag{};
  if (xml[cursor] == '/') {
    tag.closing = true;
    ++cursor;
  }
  const std::size_t name_begin = cursor;
  while (cursor < xml.size() && xml[cursor] != '>' && !std::isspace(static_cast<unsigned char>(xml[cursor])) &&
         xml[cursor] != '/') {
    ++cursor;
  }
  tag.name.assign(xml.substr(name_begin, cursor - name_begin));

  while (cursor < xml.size() && xml[cursor] != '>') {
    while (cursor < xml.size() && std::isspace(static_cast<unsigned char>(xml[cursor]))) {
      ++cursor;
    }
    if (cursor >= xml.size()) {
      return false;
    }
    if (xml[cursor] == '/') {
      tag.self_closing = true;
      ++cursor;
      continue;
    }
    if (xml[cursor] == '>') {
      break;
    }
    const std::size_t key_begin = cursor;
    while (cursor < xml.size() && xml[cursor] != '=' && xml[cursor] != '>' &&
           !std::isspace(static_cast<unsigned char>(xml[cursor]))) {
      ++cursor;
    }
    if (cursor >= xml.size() || xml[cursor] != '=') {
      continue;
    }
    XmlAttr attribute;
    attribute.name.assign(xml.substr(key_begin, cursor - key_begin));
    ++cursor;  // '='
    if (cursor < xml.size() && (xml[cursor] == '"' || xml[cursor] == '\'')) {
      const char quote = xml[cursor];
      ++cursor;
      const std::size_t value_begin = cursor;
      while (cursor < xml.size() && xml[cursor] != quote) {
        ++cursor;
      }
      attribute.value.assign(xml.substr(value_begin, cursor - value_begin));
      if (cursor < xml.size()) {
        ++cursor;
      }
    }
    decode_entities(attribute.value);
    tag.attrs.push_back(std::move(attribute));
  }
  if (cursor < xml.size()) {
    ++cursor;  // '>'
  }
  return true;
}

// XPS 的颜色：#RRGGBB 或 #AARRGGBB（透明度先忽略）。
Vec3 parse_color(std::string_view text, Vec3 fallback) {
  if (text.empty() || text.front() != '#') {
    return fallback;
  }
  const std::string hex = to_lower(text.substr(1));
  if (hex.size() != 6 && hex.size() != 8) {
    return fallback;
  }
  const auto channel = [&](std::size_t index) {
    const auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return 0;
    };
    return (nibble(hex[index]) * 16 + nibble(hex[index + 1])) / 255.f;
  };
  const std::size_t base = hex.size() == 8 ? 2 : 0;
  return Vec3{channel(base), channel(base + 2), channel(base + 4)};
}

std::array<double, 6> parse_matrix(std::string_view text) {
  std::array<double, 6> m{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  const char* cursor = text.data();
  const char* end = text.data() + text.size();
  for (double& value : m) {
    while (cursor < end && !std::isdigit(static_cast<unsigned char>(*cursor)) && *cursor != '-' &&
           *cursor != '+' && *cursor != '.') {
      ++cursor;
    }
    if (cursor >= end) {
      break;
    }
    char* next = nullptr;
    value = std::strtod(cursor, &next);
    if (next == cursor) {
      break;
    }
    cursor = next;
  }
  return m;
}

Vec2 apply_matrix(const std::array<double, 6>& m, double x, double y) {
  return Vec2{static_cast<float>(m[0] * x + m[2] * y + m[4]),
              static_cast<float>(m[1] * x + m[3] * y + m[5])};
}

// ------------------------------------------------- XPS 路径数据（mini-language）

struct NumberScanner {
  std::string_view text;
  std::size_t cursor = 0;

  void skip_separators() {
    while (cursor < text.size() &&
           (std::isspace(static_cast<unsigned char>(text[cursor])) || text[cursor] == ',')) {
      ++cursor;
    }
  }

  bool at_end() {
    skip_separators();
    return cursor >= text.size();
  }

  bool peek_command() {
    skip_separators();
    if (cursor >= text.size()) {
      return false;
    }
    const char c = text[cursor];
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  }

  bool read_number(double& out) {
    skip_separators();
    if (cursor >= text.size()) {
      return false;
    }
    char* next = nullptr;
    out = std::strtod(text.data() + cursor, &next);
    if (next == text.data() + cursor) {
      return false;
    }
    cursor = static_cast<std::size_t>(next - text.data());
    return true;
  }

  bool read_flag(bool& out) {
    double value = 0.0;
    if (!read_number(value)) {
      return false;
    }
    out = value != 0.0;
    return true;
  }
};

// 端点式圆弧 → 折线（SVG/XPS 同一套参数化）。rx/ry 为 0 时按直线处理。
void append_arc(std::vector<Vec2>& points, Vec2 from, double rx, double ry, double rotation_deg,
                bool large_arc, bool sweep, Vec2 to) {
  if (rx <= 0.0 || ry <= 0.0) {
    points.push_back(to);
    return;
  }
  rx = std::abs(rx);
  ry = std::abs(ry);
  const double phi = rotation_deg * kPi / 180.0;
  const double cos_phi = std::cos(phi);
  const double sin_phi = std::sin(phi);
  const double dx2 = (from.x - to.x) / 2.0;
  const double dy2 = (from.y - to.y) / 2.0;
  const double x1p = cos_phi * dx2 + sin_phi * dy2;
  const double y1p = -sin_phi * dx2 + cos_phi * dy2;
  const double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
  if (lambda > 1.0) {
    const double scale = std::sqrt(lambda);
    rx *= scale;
    ry *= scale;
  }
  const double sign = large_arc != sweep ? 1.0 : -1.0;
  double numerator = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
  numerator = std::max(0.0, numerator);
  const double denominator = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
  const double coefficient =
      denominator > 0.0 ? sign * std::sqrt(numerator / denominator) : 0.0;
  const double cxp = coefficient * (rx * y1p / ry);
  const double cyp = coefficient * (-ry * x1p / rx);
  const double cx = cos_phi * cxp - sin_phi * cyp + (from.x + to.x) / 2.0;
  const double cy = sin_phi * cxp + cos_phi * cyp + (from.y + to.y) / 2.0;
  const auto angle_of = [](double ux, double uy, double vx, double vy) {
    return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  };
  const double ux = (x1p - cxp) / rx;
  const double uy = (y1p - cyp) / ry;
  const double vx = (-x1p - cxp) / rx;
  const double vy = (-y1p - cyp) / ry;
  double start = std::atan2(uy, ux);
  double sweep_angle = angle_of(ux, uy, vx, vy);
  if (!sweep && sweep_angle > 0.0) {
    sweep_angle -= 2.0 * kPi;
  } else if (sweep && sweep_angle < 0.0) {
    sweep_angle += 2.0 * kPi;
  }
  const int steps = std::clamp(static_cast<int>(std::abs(sweep_angle) / (kPi / 16.0)) + 1, 2, 96);
  for (int i = 1; i <= steps; ++i) {
    const double angle = start + sweep_angle * (static_cast<double>(i) / steps);
    const double px = std::cos(angle) * rx;
    const double py = std::sin(angle) * ry;
    points.push_back(Vec2{static_cast<float>(cos_phi * px - sin_phi * py + cx),
                          static_cast<float>(sin_phi * px + cos_phi * py + cy)});
  }
}

struct ParsedPathData {
  std::vector<Vec2> points;
  bool closed = false;
};

// XPS 的 Path.Data 与 SVG 同一套命令：M/L/H/V/C/Q/A/Z（大小写对应绝对/相对）。
ParsedPathData parse_path_data(std::string_view data) {
  ParsedPathData out;
  NumberScanner scanner{data, 0};
  Vec2 current{0.f, 0.f};
  Vec2 start{0.f, 0.f};
  char command = 0;

  const auto flush_subpath = [&](bool force_close) {
    if (force_close && !out.points.empty()) {
      out.points.push_back(start);
      out.closed = true;
    }
  };

  while (true) {
    scanner.skip_separators();
    if (scanner.cursor >= data.size()) {
      break;
    }
    const char c = data[scanner.cursor];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
      command = c;
      ++scanner.cursor;
    } else if (command == 0) {
      break;  // 数据以数字开头：没有命令，丢掉
    }
    const bool relative = command >= 'a' && command <= 'z';
    const char upper = static_cast<char>(command >= 'a' ? command - 'a' + 'A' : command);
    double a = 0.0;
    double b = 0.0;

    switch (upper) {
      case 'M':
      case 'L': {
        if (!scanner.read_number(a) || !scanner.read_number(b)) {
          return out;
        }
        const Vec2 target = relative ? Vec2{static_cast<float>(current.x + a),
                                            static_cast<float>(current.y + b)}
                                     : Vec2{static_cast<float>(a), static_cast<float>(b)};
        if (upper == 'M') {
          current = target;
          start = target;
          out.points.push_back(target);
          command = relative ? 'l' : 'L';  // M 之后的后续坐标对按 L 处理
        } else {
          current = target;
          out.points.push_back(target);
        }
        break;
      }
      case 'H': {
        if (!scanner.read_number(a)) {
          return out;
        }
        current = relative ? Vec2{static_cast<float>(current.x + a), current.y}
                           : Vec2{static_cast<float>(a), current.y};
        out.points.push_back(current);
        break;
      }
      case 'V': {
        if (!scanner.read_number(a)) {
          return out;
        }
        current = relative ? Vec2{current.x, static_cast<float>(current.y + a)}
                           : Vec2{current.x, static_cast<float>(a)};
        out.points.push_back(current);
        break;
      }
      case 'C': {
        double c1x = 0.0, c1y = 0.0, c2x = 0.0, c2y = 0.0, x = 0.0, y = 0.0;
        if (!scanner.read_number(c1x) || !scanner.read_number(c1y) ||
            !scanner.read_number(c2x) || !scanner.read_number(c2y) || !scanner.read_number(x) ||
            !scanner.read_number(y)) {
          return out;
        }
        const Vec2 p0 = current;
        const Vec2 p1 = relative ? Vec2{static_cast<float>(p0.x + c1x), static_cast<float>(p0.y + c1y)}
                                 : Vec2{static_cast<float>(c1x), static_cast<float>(c1y)};
        const Vec2 p2 = relative ? Vec2{static_cast<float>(p0.x + c2x), static_cast<float>(p0.y + c2y)}
                                 : Vec2{static_cast<float>(c2x), static_cast<float>(c2y)};
        const Vec2 p3 = relative ? Vec2{static_cast<float>(p0.x + x), static_cast<float>(p0.y + y)}
                                 : Vec2{static_cast<float>(x), static_cast<float>(y)};
        // 三次贝塞尔按 16 段采样：图纸曲线够用，渲染侧本来也是画折线。
        for (int i = 1; i <= 16; ++i) {
          const float t = static_cast<float>(i) / 16.f;
          const float mt = 1.f - t;
          out.points.push_back(Vec2{mt * mt * mt * p0.x + 3.f * mt * mt * t * p1.x +
                                        3.f * mt * t * t * p2.x + t * t * t * p3.x,
                                    mt * mt * mt * p0.y + 3.f * mt * mt * t * p1.y +
                                        3.f * mt * t * t * p2.y + t * t * t * p3.y});
        }
        current = p3;
        break;
      }
      case 'Q': {
        double cx = 0.0, cy = 0.0, x = 0.0, y = 0.0;
        if (!scanner.read_number(cx) || !scanner.read_number(cy) || !scanner.read_number(x) ||
            !scanner.read_number(y)) {
          return out;
        }
        const Vec2 p0 = current;
        const Vec2 control =
            relative ? Vec2{static_cast<float>(p0.x + cx), static_cast<float>(p0.y + cy)}
                     : Vec2{static_cast<float>(cx), static_cast<float>(cy)};
        const Vec2 p2 = relative ? Vec2{static_cast<float>(p0.x + x), static_cast<float>(p0.y + y)}
                                 : Vec2{static_cast<float>(x), static_cast<float>(y)};
        for (int i = 1; i <= 12; ++i) {
          const float t = static_cast<float>(i) / 12.f;
          const float mt = 1.f - t;
          out.points.push_back(Vec2{mt * mt * p0.x + 2.f * mt * t * control.x + t * t * p2.x,
                                    mt * mt * p0.y + 2.f * mt * t * control.y + t * t * p2.y});
        }
        current = p2;
        break;
      }
      case 'A': {
        double rx = 0.0, ry = 0.0, rotation = 0.0, x = 0.0, y = 0.0;
        bool large_arc = false;
        bool sweep = false;
        if (!scanner.read_number(rx) || !scanner.read_number(ry) || !scanner.read_number(rotation) ||
            !scanner.read_flag(large_arc) || !scanner.read_flag(sweep) ||
            !scanner.read_number(x) || !scanner.read_number(y)) {
          return out;
        }
        const Vec2 from = current;
        const Vec2 to = relative ? Vec2{static_cast<float>(from.x + x), static_cast<float>(from.y + y)}
                                 : Vec2{static_cast<float>(x), static_cast<float>(y)};
        append_arc(out.points, from, rx, ry, rotation, large_arc, sweep, to);
        current = to;
        break;
      }
      case 'Z': {
        flush_subpath(true);
        current = start;
        break;
      }
      default:
        return out;  // 不认识（S/T 等）：已经解析出来的照用
    }
  }
  // 单点路径不是一条线，丢掉（闭合路径除外）。
  if (out.points.size() < 2) {
    out.points.clear();
  }
  return out;
}

}  // namespace

namespace {

constexpr const char* kDwfLayerName = "DWFx";

bool is_image_entry(std::string_view name) {
  return ends_with_ci(name, ".png") || ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg") ||
         ends_with_ci(name, ".bmp");
}

bool is_preview_entry(std::string_view name) {
  const std::string lower = to_lower(name);
  return lower.find("preview") != std::string::npos ||
         lower.find("thumbnail") != std::string::npos;
}

// XPS 的一页（FixedPage）：Path.Data → 折线，Glyphs → 文字。
// XPS 是 Y 向下、原点在左上；这里按页高翻成**Y 向上**的图纸坐标，
// 之后就和 DXF 走同一套渲染。
void parse_fixed_page(std::string_view xml, DwfContent& out) {
  DwfPage page;
  page.path_begin = out.drawing.paths().size();
  page.text_begin = out.drawing.texts().size();
  const std::uint32_t layer = out.drawing.ensure_layer(kDwfLayerName, Vec3{0.f, 0.f, 0.f});

  double page_width = 0.0;
  double page_height = 0.0;
  Aabb2 geometry_bounds{};

  std::size_t cursor = 0;
  XmlTag tag;
  bool path_open = false;
  std::string data;
  std::string stroke;
  std::string fill;
  std::array<double, 6> matrix{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};

  const auto flip = [](Vec2 p, double height) {
    return Vec2{p.x, static_cast<float>(height) - p.y};
  };

  // 先量页尺寸：翻 Y 要用页高，所以得在落点之前知道。
  {
    std::size_t probe_cursor = 0;
    XmlTag probe;
    while (next_tag(xml, probe_cursor, probe)) {
      if (probe.name == "FixedPage" && !probe.closing) {
        page_width = probe.number("Width", 0.0);
        page_height = probe.number("Height", 0.0);
        break;
      }
    }
  }

  const auto emit_path = [&]() {
    if (!data.empty()) {
      const ParsedPathData parsed = parse_path_data(data);
      if (parsed.points.size() >= 2) {
        DrawingPath path;
        path.layer = layer;
        path.kind = DrawingPathKind::Polyline;
        path.closed = parsed.closed;
        path.color = parse_color(stroke.empty() ? fill : stroke, Vec3{0.f, 0.f, 0.f});
        path.points.reserve(parsed.points.size());
        for (const Vec2 point : parsed.points) {
          const Vec2 placed = apply_matrix(matrix, point.x, point.y);
          const Vec2 world = flip(placed, page_height);
          geometry_bounds.expand(world.x, world.y);
          path.points.push_back(world);
        }
        out.drawing.add_path(std::move(path));
      }
    }
    path_open = false;
    data.clear();
    stroke.clear();
    fill.clear();
    matrix = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  };

  while (next_tag(xml, cursor, tag)) {
    const std::string& name = tag.name;
    if (name == "Path") {
      if (tag.closing) {
        emit_path();
        continue;
      }
      if (path_open) {
        emit_path();  // 容错：上一个 Path 忘了闭合
      }
      path_open = true;
      const std::string* value = tag.attr("Data");
      data = value != nullptr ? *value : std::string();
      value = tag.attr("Stroke");
      stroke = value != nullptr ? *value : std::string();
      value = tag.attr("Fill");
      fill = value != nullptr ? *value : std::string();
      if (tag.self_closing) {
        emit_path();
      }
      continue;
    }
    if (name == "MatrixTransform") {
      const std::string* value = tag.attr("Matrix");
      if (value != nullptr) {
        matrix = parse_matrix(*value);
      }
      continue;
    }
    if (name == "ImageBrush" || name == "VisualBrush") {
      ++out.skipped_images;
      continue;
    }
    if (name == "Glyphs") {
      const std::string* unicode = tag.attr("UnicodeString");
      const double em = tag.number("FontRenderingEmSize", 0.0);
      // 空串在 XPS 里写成 "{}"；没有 UnicodeString 的（纯字形索引）还原不出文字。
      if (unicode == nullptr || unicode->empty() || *unicode == "{}" || !(em > 0.0)) {
        ++out.skipped_glyphs;
        continue;
      }
      DrawingText text;
      text.text = *unicode;
      text.height = static_cast<float>(em * 0.7);  // em ≈ 字高，取大写高那一段
      const Vec2 origin = flip(Vec2{static_cast<float>(tag.number("OriginX", 0.0)),
                                    static_cast<float>(tag.number("OriginY", 0.0))},
                               page_height);
      text.position = origin;
      text.layer = layer;
      const std::string* glyph_fill = tag.attr("Fill");
      text.color = parse_color(glyph_fill != nullptr ? *glyph_fill : std::string(), Vec3{0.f, 0.f, 0.f});
      out.drawing.add_text(std::move(text));
      geometry_bounds.expand(origin.x, origin.y);
      continue;
    }
  }
  if (path_open) {
    emit_path();
  }

  if (page_width > 0.0 && page_height > 0.0) {
    page.bounds = Aabb2{};
    page.bounds.expand(0.f, 0.f);
    page.bounds.expand(static_cast<float>(page_width), static_cast<float>(page_height));
  } else {
    page.bounds = geometry_bounds;
  }
  page.path_end = out.drawing.paths().size();
  page.text_end = out.drawing.texts().size();
  out.pages.push_back(page);
}

}  // namespace

Result<DwfContent> read_dwf(std::span<const std::uint8_t> bytes) {
  if (!ZipArchive::looks_like_zip(bytes)) {
    return Err("DWF: not a zip package (expected a DWF/DWFx file)");
  }
  auto archive_result = ZipArchive::open(bytes);
  if (!archive_result) {
    return Err(std::string("DWF: ") + archive_result.error());
  }
  const ZipArchive& archive = *archive_result;

  DwfContent out;
  // DWFx（Autodesk 用 XPS 包出来的那种）= 一堆 .fpage，一页一个 FixedPage。
  std::vector<std::string> pages;
  for (const std::string& name : archive.names()) {
    if (ends_with_ci(name, ".fpage")) {
      pages.push_back(name);
    }
  }
  std::sort(pages.begin(), pages.end());
  for (const std::string& name : pages) {
    auto xml = archive.extract(name);
    if (!xml) {
      continue;
    }
    parse_fixed_page(std::string_view(reinterpret_cast<const char*>(xml->data()), xml->size()), out);
  }
  out.xps = !pages.empty();

  // 二进制 DWF（W2D 矢量流）还没解：退一步，把包里的预览位图拿出来看。
  if (!out.has_vector()) {
    std::vector<std::uint8_t> best;
    bool best_is_preview = false;
    for (const std::string& name : archive.names()) {
      if (!is_image_entry(name)) {
        continue;
      }
      const bool preview = is_preview_entry(name);
      if (!best.empty() && (best_is_preview || !preview)) {
        continue;
      }
      if (auto data = archive.extract(name); data && !data->empty()) {
        best = std::move(*data);
        best_is_preview = preview;
      }
    }
    out.preview = std::move(best);
  }

  if (!out.has_vector() && !out.has_preview()) {
    return Err(out.xps
                   ? "DWF: the DWFx package has no drawable page"
                   : "DWF: binary DWF (W2D) vector content is not supported yet, and this "
                     "package carries no preview image; re-export as DWFx, DXF or PDF");
  }
  if (out.pages.empty()) {
    out.pages.push_back(DwfPage{});
  }
  return out;
}

Result<DwfContent> read_dwf_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Err("DWF: cannot open file");
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
  if (bytes.empty()) {
    return Err("DWF: file is empty");
  }
  return read_dwf(bytes);
}

}  // namespace tamias
