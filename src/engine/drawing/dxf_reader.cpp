#include "engine/drawing/dxf_reader.h"

#include "engine/core/fs_utf8.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tamias {
namespace {

constexpr double kPi = 3.14159265358979323846;
// 圆弧/圆的离散步长：整圆 48 段（7.5°）。图纸是参考底图，够用且省内存。
constexpr double kMaxArcStep = kPi / 24.0;
constexpr int kMaxInsertDepth = 8;

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return s;
}

// 手写数字解析：DXF 组码永远是 '.' 小数点且不受 locale 影响，不依赖 strtod。
bool parse_int(std::string_view s, int& out) {
  s = trim(s);
  if (s.empty()) {
    return false;
  }
  std::size_t i = 0;
  bool negative = false;
  if (s[0] == '+' || s[0] == '-') {
    negative = s[0] == '-';
    i = 1;
  }
  if (i >= s.size()) {
    return false;
  }
  long long value = 0;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') {
      return false;
    }
    value = value * 10 + (s[i] - '0');
    if (value > 1000000000LL) {
      return false;
    }
  }
  out = static_cast<int>(negative ? -value : value);
  return true;
}

bool parse_double(std::string_view s, double& out) {
  s = trim(s);
  if (s.empty()) {
    return false;
  }
  std::size_t i = 0;
  bool negative = false;
  if (s[i] == '+' || s[i] == '-') {
    negative = s[i] == '-';
    ++i;
  }
  double mantissa = 0.0;
  bool any_digit = false;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
    mantissa = mantissa * 10.0 + static_cast<double>(s[i] - '0');
    any_digit = true;
    ++i;
  }
  if (i < s.size() && s[i] == '.') {
    ++i;
    double scale = 0.1;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      mantissa += static_cast<double>(s[i] - '0') * scale;
      scale *= 0.1;
      any_digit = true;
      ++i;
    }
  }
  if (!any_digit) {
    return false;
  }
  int exponent = 0;
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    bool exponent_negative = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
      exponent_negative = s[i] == '-';
      ++i;
    }
    bool any_exponent = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      exponent = exponent * 10 + (s[i] - '0');
      any_exponent = true;
      ++i;
    }
    if (!any_exponent) {
      return false;
    }
    if (exponent_negative) {
      exponent = -exponent;
    }
  }
  if (i != s.size()) {
    return false;
  }
  const double value = mantissa * std::pow(10.0, static_cast<double>(exponent));
  out = negative ? -value : value;
  return true;
}

// 一个 DXF 组码对（代码行 + 值行）。value 指向原文本，调用方保证文本存活。
struct Pair {
  int code = 0;
  std::string_view value;
};

// 关键字比较：行尾空白不算内容。CRLF 文件的值行会拖一个 '\r'，
// 不处理就会变成 "SECTION\r" != "SECTION"，整个 ENTITIES 段被跳过。
std::string_view keyword(std::string_view value) {
  while (!value.empty() &&
         (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
    value.remove_suffix(1);
  }
  return value;
}

bool tokenize(std::string_view text, std::vector<Pair>& out) {
  std::size_t pos = 0;
  const std::size_t size = text.size();
  while (pos < size) {
    std::size_t line_end = text.find('\n', pos);
    if (line_end == std::string_view::npos) {
      line_end = size;
    }
    std::string_view code_line = text.substr(pos, line_end - pos);
    pos = line_end < size ? line_end + 1 : size;
    int code = 0;
    if (!parse_int(code_line, code)) {
      continue;  // 空行 / 垃圾行，跳过
    }
    if (pos >= size) {
      break;
    }
    std::size_t value_end = text.find('\n', pos);
    if (value_end == std::string_view::npos) {
      value_end = size;
    }
    std::string_view value = text.substr(pos, value_end - pos);
    if (!value.empty() && value.back() == '\r') {
      value.remove_suffix(1);  // CRLF：值里不该带行尾 CR
    }
    out.push_back({code, value});
    pos = value_end < size ? value_end + 1 : size;
  }
  return !out.empty();
}

// 2D 仿射：p' = [a c e; b d f] · (x, y, 1)。
struct Xform2 {
  double a = 1.0;
  double b = 0.0;
  double c = 0.0;
  double d = 1.0;
  double e = 0.0;
  double f = 0.0;

  [[nodiscard]] static Xform2 identity() { return {}; }

  [[nodiscard]] static Xform2 translation(double tx, double ty) {
    Xform2 m;
    m.e = tx;
    m.f = ty;
    return m;
  }

  [[nodiscard]] static Xform2 rotation(double radians) {
    Xform2 m;
    const double cs = std::cos(radians);
    const double sn = std::sin(radians);
    m.a = cs;
    m.b = sn;
    m.c = -sn;
    m.d = cs;
    return m;
  }

  [[nodiscard]] static Xform2 scaling(double sx, double sy) {
    Xform2 m;
    m.a = sx;
    m.d = sy;
    return m;
  }

  [[nodiscard]] Vec2 apply(Vec2 p) const {
    return {static_cast<float>(a * p.x + c * p.y + e),
            static_cast<float>(b * p.x + d * p.y + f)};
  }

  // this ∘ inner：先应用 inner，再应用 this。
  [[nodiscard]] Xform2 compose(const Xform2& inner) const {
    Xform2 r;
    r.a = a * inner.a + c * inner.b;
    r.b = b * inner.a + d * inner.b;
    r.c = a * inner.c + c * inner.d;
    r.d = b * inner.c + d * inner.d;
    r.e = a * inner.e + c * inner.f + e;
    r.f = b * inner.e + d * inner.f + f;
    return r;
  }
};

Vec3 hsv_to_rgb(double h_deg, double s, double v) {
  const double h = h_deg / 60.0;
  const int sector = static_cast<int>(std::floor(h)) % 6;
  const double frac = h - std::floor(h);
  const double p = v * (1.0 - s);
  const double q = v * (1.0 - s * frac);
  const double t = v * (1.0 - s * (1.0 - frac));
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  switch (sector) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return {static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)};
}

// AutoCAD 颜色索引 → RGB。1–9 是标准色；10–249 按 24 色相 × 10 档近似；
// 250–255 是灰阶。想要逐格精确需要整张 256 色表，参考底图不值得。
Vec3 aci_color(int index) {
  static const Vec3 kStandard[10] = {
      {0.f, 0.f, 0.f},        // 0 = ByBlock
      {1.f, 0.f, 0.f},        // 1 红
      {1.f, 1.f, 0.f},        // 2 黄
      {0.f, 1.f, 0.f},        // 3 绿
      {0.f, 1.f, 1.f},        // 4 青
      {0.f, 0.f, 1.f},        // 5 蓝
      {1.f, 0.f, 1.f},        // 6 洋红
      {1.f, 1.f, 1.f},        // 7 白/黑（按背景取对比色，看图默认深底）
      {0.5f, 0.5f, 0.5f},     // 8 深灰
      {0.75f, 0.75f, 0.75f},  // 9 浅灰
  };
  if (index >= 0 && index <= 9) {
    return kStandard[index];
  }
  if (index >= 250) {
    const double g = index == 250 ? 0.2 : 0.2 + 0.15 * (index - 250);
    return {static_cast<float>(std::min(g, 0.95)), static_cast<float>(std::min(g, 0.95)),
            static_cast<float>(std::min(g, 0.95))};
  }
  const int hue = (index - 10) % 24;
  const int shade = (index - 10) / 24;
  const double h = hue * 15.0;
  const double s = 1.0 - 0.055 * shade;
  const double v = shade < 5 ? 1.0 : 1.0 - 0.12 * (shade - 4);
  return hsv_to_rgb(h, std::clamp(s, 0.3, 1.0), std::clamp(v, 0.25, 1.0));
}

Vec3 true_color(int value) {
  const int r = (value >> 16) & 0xFF;
  const int g = (value >> 8) & 0xFF;
  const int b = value & 0xFF;
  return {static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f,
          static_cast<float>(b) / 255.f};
}

struct BlockDef {
  std::string name;
  std::vector<Pair> pairs;  // 块体（不含 BLOCK / ENDBLK），保留 (0,TYPE) 组码
  Vec2 base{};
};

// ---- 组码取值 ----

bool group_has(const std::vector<Pair>& p, std::size_t begin, std::size_t end, int code) {
  for (std::size_t i = begin; i < end; ++i) {
    if (p[i].code == code) {
      return true;
    }
  }
  return false;
}

double group_num(const std::vector<Pair>& p, std::size_t begin, std::size_t end, int code,
                 double fallback) {
  for (std::size_t i = begin; i < end; ++i) {
    if (p[i].code == code) {
      double value = 0.0;
      if (parse_double(p[i].value, value)) {
        return value;
      }
    }
  }
  return fallback;
}

int group_int(const std::vector<Pair>& p, std::size_t begin, std::size_t end, int code,
              int fallback) {
  for (std::size_t i = begin; i < end; ++i) {
    if (p[i].code == code) {
      int value = 0;
      if (parse_int(p[i].value, value)) {
        return value;
      }
    }
  }
  return fallback;
}

std::string_view group_str(const std::vector<Pair>& p, std::size_t begin, std::size_t end,
                           int code) {
  for (std::size_t i = begin; i < end; ++i) {
    if (p[i].code == code) {
      return p[i].value;
    }
  }
  return {};
}

// MTEXT 的格式码：\P 换行，\f…; 之类是字体/样式，去掉。
std::string clean_mtext(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) {
    const char ch = raw[i];
    if (ch == '\\' && i + 1 < raw.size()) {
      const char next = raw[i + 1];
      if (next == 'P' || next == 'p') {
        out.push_back('\n');
        ++i;
        continue;
      }
      if ((next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z')) {
        // 跳过到分号为止（\fArial|b0|i0;、\H2.5x; 等）
        std::size_t j = i + 2;
        while (j < raw.size() && raw[j] != ';') {
          ++j;
        }
        i = j < raw.size() ? j : raw.size() - 1;
        continue;
      }
    }
    if (ch == '{' || ch == '}') {
      continue;
    }
    out.push_back(ch);
  }
  return out;
}

class DxfParser {
 public:
  DxfParser(const std::vector<Pair>& pairs, Drawing& drawing)
      : pairs_(pairs), drawing_(drawing) {}

  Result<void> run();
  // "SPLINE ×80, HATCH ×40"：给"什么都画不出来"时的错误信息用。
  [[nodiscard]] std::string unsupported_summary() const;

 private:
  std::size_t skip_section(std::size_t i) const;
  std::size_t parse_header(std::size_t i);
  std::size_t parse_tables(std::size_t i);
  std::size_t parse_blocks(std::size_t i);
  std::size_t process_entities(const std::vector<Pair>& p, std::size_t i, const Xform2& x,
                               int depth);
  std::size_t handle_entity(const std::vector<Pair>& p, std::size_t i, std::size_t end,
                            const Xform2& x, int depth);

  void emit_path(const Xform2& x, std::vector<Vec2> points, bool closed, std::uint32_t layer,
                 Vec3 color, DrawingPathKind kind = DrawingPathKind::Polyline);
  void emit_arc(const Xform2& x, Vec2 center, double radius, double start_rad, double sweep_rad,
                bool closed, std::uint32_t layer, Vec3 color,
                DrawingPathKind kind = DrawingPathKind::Arc);

  [[nodiscard]] std::uint32_t layer_index_for(std::string_view name);
  void register_layer(std::string_view name, Vec3 color);
  [[nodiscard]] Vec3 resolve_color(int aci, int true_color_value, Vec3 layer_color) const;
  void note_unsupported(std::string_view type);

  const std::vector<Pair>& pairs_;
  Drawing& drawing_;
  std::unordered_map<std::string, BlockDef> blocks_;
  std::unordered_map<std::string, std::uint32_t> layer_index_;

  // 块上下文：块内画在 0 层的图元按 DXF 语义继承块引用的图层；
  // 块名本身是门窗识别的关键线索（M0921 / C1518 这类编号）。
  std::string current_block_;
  std::string current_block_layer_;
  float current_elevation_ = 0.f;  // 当前块基点的标高（块递归时累加）
  float emit_elevation_ = 0.f;     // 正在输出这条路径的标高
  std::unordered_map<std::string, Vec3> layer_colors_;
  std::unordered_map<std::string, std::size_t> unsupported_types_;
};

void DxfParser::note_unsupported(std::string_view type) {
  drawing_.add_unsupported_entity();
  ++unsupported_types_[std::string(type)];
}

// "SPLINE ×80, HATCH ×40" —— 按数量从多到少，最多列 4 种。
std::string DxfParser::unsupported_summary() const {
  std::vector<std::pair<std::string, std::size_t>> sorted(unsupported_types_.begin(),
                                                          unsupported_types_.end());
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
    return a.second != b.second ? a.second > b.second : a.first < b.first;
  });
  std::string text;
  for (std::size_t i = 0; i < sorted.size() && i < 4; ++i) {
    if (i > 0) {
      text += ", ";
    }
    text += sorted[i].first + " ×" + std::to_string(sorted[i].second);
  }
  if (sorted.size() > 4) {
    text += ", …";
  }
  return text;
}

void DxfParser::register_layer(std::string_view name, Vec3 color) {
  if (name.empty()) {
    return;
  }
  const std::string key(name);
  layer_colors_[key] = color;
  (void)layer_index_for(key);
}

std::uint32_t DxfParser::layer_index_for(std::string_view name) {
  std::string key = name.empty() ? std::string("0") : std::string(name);
  if (const auto it = layer_index_.find(key); it != layer_index_.end()) {
    return it->second;
  }
  Vec3 color{1.f, 1.f, 1.f};
  if (const auto c = layer_colors_.find(key); c != layer_colors_.end()) {
    color = c->second;
  }
  const std::uint32_t index = drawing_.ensure_layer(key, color);
  layer_index_.emplace(std::move(key), index);
  return index;
}

Vec3 DxfParser::resolve_color(int aci, int true_color_value, Vec3 layer_color) const {
  if (true_color_value >= 0) {
    return true_color(true_color_value);
  }
  if (aci <= 0 || aci == 256) {
    return layer_color;  // BYBLOCK / BYLAYER
  }
  return aci_color(aci);
}

void DxfParser::emit_path(const Xform2& x, std::vector<Vec2> points, bool closed,
                          std::uint32_t layer, Vec3 color, DrawingPathKind kind) {
  if (points.size() < 2) {
    return;
  }
  DrawingPath path;
  path.points.reserve(points.size());
  for (const Vec2 p : points) {
    path.points.push_back(x.apply(p));
  }
  path.closed = closed;
  path.layer = layer;
  path.color = color;
  path.kind = kind;
  path.block = current_block_;
  path.elevation = emit_elevation_;
  drawing_.add_path(std::move(path));
}

void DxfParser::emit_arc(const Xform2& x, Vec2 center, double radius, double start_rad,
                         double sweep_rad, bool closed, std::uint32_t layer, Vec3 color,
                         DrawingPathKind kind) {
  if (!(radius > 1e-9) || std::fabs(sweep_rad) < 1e-12) {
    return;
  }
  const int segments =
      std::clamp(static_cast<int>(std::ceil(std::fabs(sweep_rad) / kMaxArcStep)), 2, 512);
  std::vector<Vec2> points;
  points.reserve(static_cast<std::size_t>(segments) + 1);
  for (int i = 0; i <= segments; ++i) {
    const double t = start_rad + sweep_rad * (static_cast<double>(i) / segments);
    points.push_back({static_cast<float>(center.x + radius * std::cos(t)),
                      static_cast<float>(center.y + radius * std::sin(t))});
  }
  if (closed) {
    points.pop_back();  // 闭合环由 closed 标记，不重复首点
  }
  emit_path(x, std::move(points), closed, layer, color, kind);
}

std::size_t DxfParser::skip_section(std::size_t i) const {
  const std::size_t n = pairs_.size();
  while (i < n) {
    if (pairs_[i].code == 0 && pairs_[i].value == "ENDSEC") {
      return i + 1;
    }
    ++i;
  }
  return n;
}

// HEADER 只取翻模必需的一件东西：$INSUNITS（图纸单位）。
// 没有它，"12000" 是毫米还是米只能靠猜。
std::size_t DxfParser::parse_header(std::size_t i) {
  const std::size_t n = pairs_.size();
  while (i < n) {
    if (pairs_[i].code == 0 && keyword(pairs_[i].value) == "ENDSEC") {
      return i + 1;
    }
    if (pairs_[i].code == 9 && keyword(pairs_[i].value) == "$INSUNITS") {
      std::size_t j = i + 1;
      while (j < n && pairs_[j].code != 0 && pairs_[j].code != 9) {
        ++j;
      }
      drawing_.set_insunits(group_int(pairs_, i + 1, j, 70, 0));
      i = j;
      continue;
    }
    ++i;
  }
  return n;
}

std::size_t DxfParser::parse_tables(std::size_t i) {
  const std::size_t n = pairs_.size();
  while (i < n) {
    if (pairs_[i].code == 0) {
      if (keyword(pairs_[i].value) == "ENDSEC") {
        return i + 1;
      }
      if (keyword(pairs_[i].value) == "LAYER") {
        std::size_t j = i + 1;
        while (j < n && pairs_[j].code != 0) {
          ++j;
        }
        const std::string_view name = group_str(pairs_, i + 1, j, 2);
        int aci = group_int(pairs_, i + 1, j, 62, 7);
        const bool off = aci < 0;  // 负值 = 图层关闭；这里只取颜色
        if (off) {
          aci = -aci;
        }
        const int true_color_value =
            group_has(pairs_, i + 1, j, 420) ? group_int(pairs_, i + 1, j, 420, -1) : -1;
        register_layer(name, resolve_color(aci, true_color_value, Vec3{1.f, 1.f, 1.f}));
        i = j;
        continue;
      }
    }
    ++i;
  }
  return n;
}

std::size_t DxfParser::parse_blocks(std::size_t i) {
  const std::size_t n = pairs_.size();
  while (i < n) {
    if (pairs_[i].code == 0 && keyword(pairs_[i].value) == "ENDSEC") {
      return i + 1;
    }
    if (pairs_[i].code == 0 && keyword(pairs_[i].value) == "BLOCK") {
      std::size_t body = i + 1;
      std::size_t end = body;
      while (end < n && !(pairs_[end].code == 0 && keyword(pairs_[end].value) == "ENDBLK")) {
        ++end;
      }
      BlockDef def;
      std::size_t head = body;
      while (head < end && pairs_[head].code != 0) {
        if (pairs_[head].code == 2) {
          def.name = std::string(pairs_[head].value);
        } else if (pairs_[head].code == 10) {
          double v = 0.0;
          if (parse_double(pairs_[head].value, v)) {
            def.base.x = static_cast<float>(v);
          }
        } else if (pairs_[head].code == 20) {
          double v = 0.0;
          if (parse_double(pairs_[head].value, v)) {
            def.base.y = static_cast<float>(v);
          }
        }
        ++head;
      }
      def.pairs.assign(pairs_.begin() + static_cast<std::ptrdiff_t>(head),
                       pairs_.begin() + static_cast<std::ptrdiff_t>(end));
      if (!def.name.empty()) {
        blocks_[def.name] = std::move(def);
      }
      i = end < n ? end + 1 : n;
      continue;
    }
    ++i;
  }
  return n;
}

Result<void> DxfParser::run() {
  std::size_t i = 0;
  const std::size_t n = pairs_.size();
  while (i < n) {
    if (pairs_[i].code != 0) {
      ++i;
      continue;
    }
    if (keyword(pairs_[i].value) == "SECTION") {
      // 段名是紧跟 SECTION 的组码 2；段体在其后。这里读到段名就停，不能一路吃到
      // 下一个组码 0 —— HEADER 的段体是组码 9 开头的变量表，本来就没有前导组码 0，
      // 一路吃下去会把 $INSUNITS 整段吞掉。
      std::size_t j = i + 1;
      std::string_view name;
      if (j < n && pairs_[j].code == 2) {
        name = pairs_[j].value;
        ++j;
      } else {
        while (j < n && pairs_[j].code != 0) {
          ++j;
        }
      }
      const std::string_view section = keyword(name);
      if (section == "ENTITIES") {
        i = process_entities(pairs_, j, Xform2::identity(), 0);
      } else if (section == "BLOCKS") {
        i = parse_blocks(j);
      } else if (section == "TABLES") {
        i = parse_tables(j);
      } else if (section == "HEADER") {
        i = parse_header(j);
      } else {
        i = skip_section(j);
      }
      continue;
    }
    ++i;
  }
  return {};
}

std::size_t DxfParser::process_entities(const std::vector<Pair>& p, std::size_t i,
                                        const Xform2& x, int depth) {
  const std::size_t n = p.size();
  while (i < n) {
    if (p[i].code != 0) {
      ++i;
      continue;
    }
    const std::string_view value = keyword(p[i].value);
    if (value == "ENDSEC" || value == "SECTION") {
      return i + 1;
    }
    i = handle_entity(p, i, n, x, depth);
  }
  return n;
}

std::size_t DxfParser::handle_entity(const std::vector<Pair>& p, std::size_t i, std::size_t end,
                                     const Xform2& x, int depth) {
  const std::string_view type = keyword(p[i].value);
  std::size_t j = i + 1;
  while (j < end && p[j].code != 0) {
    ++j;
  }
  const std::size_t b = i + 1;
  const std::size_t e = j;

  const auto num = [&](int code, double fallback) {
    return group_num(p, b, e, code, fallback);
  };
  const auto has = [&](int code) { return group_has(p, b, e, code); };
  const auto str = [&](int code) { return group_str(p, b, e, code); };

  auto color_of = [&](std::uint32_t layer) {
    const int aci = has(62) ? group_int(p, b, e, 62, 256) : 256;
    const int true_color_value = has(420) ? group_int(p, b, e, 420, -1) : -1;
    Vec3 layer_color{1.f, 1.f, 1.f};
    const auto& layers = drawing_.layers();
    if (layer < layers.size()) {
      layer_color = layers[layer].color;
    }
    return resolve_color(aci, true_color_value, layer_color);
  };

  // 块内画在 0 层的图元按 DXF 语义继承块引用的图层；不处理的话门窗符号会
  // 一律落到名为 "0" 的图层上，翻模就找不到它们。
  std::string_view layer_name = str(8);
  if (!current_block_layer_.empty() && (layer_name.empty() || layer_name == "0")) {
    layer_name = current_block_layer_;
  }
  const std::uint32_t layer = layer_index_for(layer_name);
  // 标高：优先取 38（多段线/块引用的 elevation），退回 30（线段起点的 z）。
  emit_elevation_ = current_elevation_ + static_cast<float>(num(38, num(30, 0.0)));

  if (type == "LINE") {
    const Vec2 p1{static_cast<float>(num(10, 0.0)), static_cast<float>(num(20, 0.0))};
    const Vec2 p2{static_cast<float>(num(11, 0.0)), static_cast<float>(num(21, 0.0))};
    emit_path(x, {p1, p2}, false, layer, color_of(layer), DrawingPathKind::Line);
    return e;
  }
  if (type == "CIRCLE") {
    const Vec2 center{static_cast<float>(num(10, 0.0)), static_cast<float>(num(20, 0.0))};
    emit_arc(x, center, num(40, 0.0), 0.0, 2.0 * kPi, true, layer, color_of(layer),
             DrawingPathKind::Circle);
    return e;
  }
  if (type == "ARC") {
    const Vec2 center{static_cast<float>(num(10, 0.0)), static_cast<float>(num(20, 0.0))};
    const double start_deg = num(50, 0.0);
    double end_deg = num(51, 0.0);
    while (end_deg < start_deg) {
      end_deg += 360.0;  // DXF 圆弧总是从 start 逆时针扫到 end
    }
    const double start_rad = start_deg * kPi / 180.0;
    const double sweep_rad = (end_deg - start_deg) * kPi / 180.0;
    emit_arc(x, center, num(40, 0.0), start_rad, sweep_rad, false, layer, color_of(layer),
             DrawingPathKind::Arc);
    return e;
  }
  if (type == "ELLIPSE") {
    const Vec2 center{static_cast<float>(num(10, 0.0)), static_cast<float>(num(20, 0.0))};
    const Vec2 major{static_cast<float>(num(11, 0.0)), static_cast<float>(num(21, 0.0))};
    const double ratio = num(40, 1.0);
    const double start = num(41, 0.0);
    double end = num(42, 2.0 * kPi);
    while (end < start) {
      end += 2.0 * kPi;
    }
    const double sweep = end - start;
    const int segments =
        std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) / kMaxArcStep)), 2, 512);
    const Vec2 minor{-major.y * static_cast<float>(ratio),
                     major.x * static_cast<float>(ratio)};
    std::vector<Vec2> points;
    points.reserve(static_cast<std::size_t>(segments) + 1);
    for (int k = 0; k <= segments; ++k) {
      const double t = start + sweep * (static_cast<double>(k) / segments);
      const double ct = std::cos(t);
      const double st = std::sin(t);
      points.push_back({static_cast<float>(center.x + major.x * ct + minor.x * st),
                        static_cast<float>(center.y + major.y * ct + minor.y * st)});
    }
    const bool closed = std::fabs(std::fabs(sweep) - 2.0 * kPi) < 1e-9;
    if (closed) {
      points.pop_back();
    }
    emit_path(x, std::move(points), closed, layer, color_of(layer), DrawingPathKind::Ellipse);
    return e;
  }
  if (type == "LWPOLYLINE") {
    const int flags = group_int(p, b, e, 70, 0);
    const bool closed = (flags & 1) != 0;
    std::vector<Vec2> vertices;
    std::vector<double> bulges;
    bool have_x = false;
    double pending_x = 0.0;
    for (std::size_t k = b; k < e; ++k) {
      if (p[k].code == 10) {
        double v = 0.0;
        if (parse_double(p[k].value, v)) {
          pending_x = v;
          have_x = true;
        }
      } else if (p[k].code == 20) {
        double v = 0.0;
        if (have_x && parse_double(p[k].value, v)) {
          vertices.push_back({static_cast<float>(pending_x), static_cast<float>(v)});
          bulges.push_back(0.0);
          have_x = false;
        }
      } else if (p[k].code == 42 && !bulges.empty()) {
        double v = 0.0;
        if (parse_double(p[k].value, v)) {
          bulges.back() = v;
        }
      }
    }
    if (vertices.size() < 2) {
      return e;
    }
    const Vec3 color = color_of(layer);
    const std::size_t count = vertices.size();
    std::vector<Vec2> out;
    const std::size_t limit = closed ? count : count - 1;
    for (std::size_t k = 0; k < limit; ++k) {
      const Vec2 a = vertices[k];
      const Vec2 c = vertices[(k + 1) % count];
      const double bulge = bulges[k];
      if (std::fabs(bulge) < 1e-12) {
        if (out.empty()) {
          out.push_back(a);
        }
        out.push_back(c);
        continue;
      }
      const double dx = static_cast<double>(c.x) - a.x;
      const double dy = static_cast<double>(c.y) - a.y;
      const double chord = std::sqrt(dx * dx + dy * dy);
      if (chord < 1e-12) {
        continue;
      }
      const double theta = 4.0 * std::atan(bulge);
      const double radius = chord / (2.0 * std::sin(theta / 2.0));
      const double mx = (static_cast<double>(a.x) + c.x) * 0.5;
      const double my = (static_cast<double>(a.y) + c.y) * 0.5;
      const double nx = -dy / chord;
      const double ny = dx / chord;
      const double h = radius * std::cos(theta / 2.0);
      const double cx = mx + nx * h;
      const double cy = my + ny * h;
      const double start_rad = std::atan2(static_cast<double>(a.y) - cy,
                                          static_cast<double>(a.x) - cx);
      const int segments =
          std::clamp(static_cast<int>(std::ceil(std::fabs(theta) / kMaxArcStep)), 2, 512);
      if (out.empty()) {
        out.push_back(a);
      }
      for (int s = 1; s <= segments; ++s) {
        const double t = start_rad + theta * (static_cast<double>(s) / segments);
        out.push_back({static_cast<float>(cx + radius * std::cos(t)),
                       static_cast<float>(cy + radius * std::sin(t))});
      }
    }
    emit_path(x, std::move(out), closed, layer, color);
    return e;
  }
  if (type == "POLYLINE") {
    const int flags = group_int(p, b, e, 70, 0);
    const bool closed = (flags & 1) != 0;
    std::vector<Vec2> vertices;
    std::vector<double> bulges;
    std::size_t k = e;
    while (k < end) {
      if (p[k].code != 0) {
        ++k;
        continue;
      }
      if (keyword(p[k].value) == "SEQEND") {
        ++k;
        break;
      }
      if (keyword(p[k].value) != "VERTEX") {
        break;
      }
      std::size_t v_end = k + 1;
      while (v_end < end && p[v_end].code != 0) {
        ++v_end;
      }
      const double vx = group_num(p, k + 1, v_end, 10, 0.0);
      const double vy = group_num(p, k + 1, v_end, 20, 0.0);
      vertices.push_back({static_cast<float>(vx), static_cast<float>(vy)});
      bulges.push_back(group_num(p, k + 1, v_end, 42, 0.0));
      k = v_end;
    }
    if (vertices.size() >= 2) {
      // 复用 LWPOLYLINE 的路径：顶点+凸度直接展开成折线。
      const Vec3 color = color_of(layer);
      std::vector<Vec2> out;
      const std::size_t count = vertices.size();
      const std::size_t limit = closed ? count : count - 1;
      for (std::size_t v = 0; v < limit; ++v) {
        const Vec2 a = vertices[v];
        const Vec2 c = vertices[(v + 1) % count];
        const double bulge = bulges[v];
        if (std::fabs(bulge) < 1e-12) {
          if (out.empty()) {
            out.push_back(a);
          }
          out.push_back(c);
          continue;
        }
        const double dx = static_cast<double>(c.x) - a.x;
        const double dy = static_cast<double>(c.y) - a.y;
        const double chord = std::sqrt(dx * dx + dy * dy);
        if (chord < 1e-12) {
          continue;
        }
        const double theta = 4.0 * std::atan(bulge);
        const double radius = chord / (2.0 * std::sin(theta / 2.0));
        const double mx = (static_cast<double>(a.x) + c.x) * 0.5;
        const double my = (static_cast<double>(a.y) + c.y) * 0.5;
        const double nx = -dy / chord;
        const double ny = dx / chord;
        const double hh = radius * std::cos(theta / 2.0);
        const double cx = mx + nx * hh;
        const double cy = my + ny * hh;
        const double start_rad =
            std::atan2(static_cast<double>(a.y) - cy, static_cast<double>(a.x) - cx);
        const int segments =
            std::clamp(static_cast<int>(std::ceil(std::fabs(theta) / kMaxArcStep)), 2, 512);
        if (out.empty()) {
          out.push_back(a);
        }
        for (int s = 1; s <= segments; ++s) {
          const double t = start_rad + theta * (static_cast<double>(s) / segments);
          out.push_back({static_cast<float>(cx + radius * std::cos(t)),
                         static_cast<float>(cy + radius * std::sin(t))});
        }
      }
      emit_path(x, std::move(out), closed, layer, color);
    }
    return k;
  }
  if (type == "TEXT") {
    DrawingText text;
    const bool aligned = group_int(p, b, e, 72, 0) != 0 || group_int(p, b, e, 73, 0) != 0;
    const int px = aligned && has(11) ? 11 : 10;
    const int py = aligned && has(21) ? 21 : 20;
    text.position = {static_cast<float>(num(px, 0.0)), static_cast<float>(num(py, 0.0))};
    text.height = static_cast<float>(num(40, 2.5));
    text.rotation_deg = static_cast<float>(num(50, 0.0));
    text.text = std::string(str(1));
    text.layer = layer;
    text.color = color_of(layer);
    if (!text.text.empty()) {
      drawing_.add_text(std::move(text));
    }
    return e;
  }
  if (type == "MTEXT") {
    std::string raw;
    for (std::size_t k = b; k < e; ++k) {
      if (p[k].code == 3 || p[k].code == 1) {
        raw.append(p[k].value);
      }
    }
    DrawingText text;
    const double height = num(40, 2.5);
    // MTEXT 的插入点是第一行左上角，转成基线：往下压一个字高。
    text.position = {static_cast<float>(num(10, 0.0)),
                     static_cast<float>(num(20, 0.0) - height)};
    text.height = static_cast<float>(height);
    text.rotation_deg = static_cast<float>(num(50, 0.0));
    text.text = clean_mtext(raw);
    text.layer = layer;
    text.color = color_of(layer);
    if (!text.text.empty()) {
      drawing_.add_text(std::move(text));
    }
    return e;
  }
  if (type == "INSERT") {
    const std::string_view block_name = str(2);
    const auto found = blocks_.find(std::string(block_name));
    if (found == blocks_.end() || depth >= kMaxInsertDepth) {
      note_unsupported(type);
      return e;
    }
    const double ins_x = num(10, 0.0);
    const double ins_y = num(20, 0.0);
    double sx = num(41, 1.0);
    double sy = num(42, 1.0);
    if (std::fabs(sx) < 1e-12) {
      sx = 1.0;
    }
    if (std::fabs(sy) < 1e-12) {
      sy = 1.0;
    }
    const double rotation = num(50, 0.0) * kPi / 180.0;
    const int columns = std::max(1, group_int(p, b, e, 70, 1));
    const int rows = std::max(1, group_int(p, b, e, 71, 1));
    const double column_spacing = num(44, 0.0);
    const double row_spacing = num(45, 0.0);
    const BlockDef& def = found->second;
    // 块上下文：块内 0 层继承块引用的图层，块名一路带到路径上（翻模靠它认门窗）。
    const std::string saved_block = current_block_;
    const std::string saved_layer = current_block_layer_;
    const float saved_elevation = current_elevation_;
    if (current_block_.empty()) {
      current_block_ = std::string(block_name);
    }
    current_block_layer_ = std::string(str(8));
    current_elevation_ = saved_elevation + static_cast<float>(num(30, 0.0));
    for (int row = 0; row < rows; ++row) {
      for (int col = 0; col < columns; ++col) {
        // 阵列偏移在块自己的坐标系里，先加偏移再旋转/缩放。
        const double local_x = ins_x + static_cast<double>(col) * column_spacing;
        const double local_y = ins_y + static_cast<double>(row) * row_spacing;
        Xform2 block_x = Xform2::translation(local_x, local_y)
                             .compose(Xform2::rotation(rotation))
                             .compose(Xform2::scaling(sx, sy))
                             .compose(Xform2::translation(-def.base.x, -def.base.y));
        process_entities(def.pairs, 0, x.compose(block_x), depth + 1);
      }
    }
    current_block_ = saved_block;
    current_block_layer_ = saved_layer;
    current_elevation_ = saved_elevation;
    return e;
  }
  if (type == "SEQEND" || type == "ENDBLK" || type == "ATTRIB" || type == "ATTDEF") {
    return e;
  }
  if (type == "POINT") {
    note_unsupported(type);  // 参考底图上画点没有意义，但要能解释"为什么是空的"
    return e;
  }
  note_unsupported(type);
  return e;
}

}  // namespace

Result<Drawing> parse_dxf(std::string_view text) {
  // UTF-8 BOM：不少 Windows 工具会写，不剥掉第一条组码行就废了。
  if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") {
    text.remove_prefix(3);
  }
  // UTF-16 / UTF-32 是两种情况：直接说清楚，别让用户看到"没有图元"。
  if (text.size() >= 2) {
    const unsigned char b0 = static_cast<unsigned char>(text[0]);
    const unsigned char b1 = static_cast<unsigned char>(text[1]);
    if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) {
      return Err("DXF is UTF-16 encoded; re-save it as ASCII/ANSI DXF");
    }
  }
  if (text.size() >= 18 && text.substr(0, 18) == "AutoCAD Binary DXF") {
    return Err("binary DXF is not supported; re-save as ASCII DXF");
  }
  if (text.empty()) {
    return Err("empty DXF file");
  }
  std::vector<Pair> pairs;
  if (!tokenize(text, pairs)) {
    return Err("not a DXF file: no group codes found");
  }
  Drawing drawing;
  DxfParser parser(pairs, drawing);
  if (auto r = parser.run(); !r) {
    return Err(r.error());
  }
  if (drawing.empty()) {
    if (drawing.unsupported_entity_count() > 0) {
      return Err("nothing to draw: skipped " +
                 std::to_string(drawing.unsupported_entity_count()) + " unsupported entities (" +
                 parser.unsupported_summary() + ")");
    }
    return Err("nothing to draw: no entities in the ENTITIES section");
  }
  drawing.normalize_origin();
  return drawing;
}

Result<Drawing> load_dxf(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Err("cannot open DXF: " + path_to_utf8(path));
  }
  const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  auto result = parse_dxf(text);
  if (!result) {
    return Err(path_to_utf8(path.filename()) + ": " + result.error());
  }
  return result;
}

}  // namespace tamias
