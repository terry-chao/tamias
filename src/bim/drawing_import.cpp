#include "bim/drawing_import.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tamias {
namespace {

constexpr double kEps = 1e-9;

std::string lower_ascii(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    out.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
  }
  return out;
}

// 逗号（中英文）/ 分号 / 空白分隔的图层关键字表；统一转小写。
std::vector<std::string> split_keywords(const std::string& text) {
  std::vector<std::string> out;
  std::string token;
  auto flush = [&out, &token] {
    if (!token.empty()) {
      out.push_back(lower_ascii(token));
      token.clear();
    }
  };
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    // 全角逗号（，= EF BC 8C）与全角分号（；= EF BC 9B）都是三个字节。
    if (ch == 0xEF && i + 2 < text.size() &&
        static_cast<unsigned char>(text[i + 1]) == 0xBC &&
        (static_cast<unsigned char>(text[i + 2]) == 0x8C ||
         static_cast<unsigned char>(text[i + 2]) == 0x9B)) {
      flush();
      i += 3;
      continue;
    }
    if (ch == ',' || ch == ';' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
      flush();
      ++i;
      continue;
    }
    token.push_back(text[i]);
    ++i;
  }
  flush();
  return out;
}

bool matches_keywords(const std::vector<std::string>& keywords, std::string_view layer_name) {
  const std::string lower = lower_ascii(layer_name);
  for (const std::string& keyword : keywords) {
    if (!keyword.empty() && lower.find(keyword) != std::string::npos) {
      return true;
    }
  }
  return false;
}

struct Bbox2 {
  float min_x = 1e30f;
  float min_y = 1e30f;
  float max_x = -1e30f;
  float max_y = -1e30f;

  void expand(Vec2 p) {
    min_x = std::min(min_x, p.x);
    min_y = std::min(min_y, p.y);
    max_x = std::max(max_x, p.x);
    max_y = std::max(max_y, p.y);
  }
  [[nodiscard]] bool valid() const { return min_x <= max_x && min_y <= max_y; }
  [[nodiscard]] Vec2 center() const { return {(min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f}; }
  [[nodiscard]] float width() const { return max_x - min_x; }
  [[nodiscard]] float height() const { return max_y - min_y; }
};

Bbox2 path_bounds(const DrawingPath& path) {
  Bbox2 box;
  for (const Vec2 p : path.points) {
    box.expand(p);
  }
  return box;
}

// 点到线段的距离，附带投影参数 t（0..1 表示落在段内）。
double point_segment_distance(Vec2 point, Vec2 a, Vec2 b, double* t_out) {
  const double dx = static_cast<double>(b.x) - a.x;
  const double dy = static_cast<double>(b.y) - a.y;
  const double len2 = dx * dx + dy * dy;
  double t = 0.0;
  if (len2 > kEps) {
    t = ((static_cast<double>(point.x) - a.x) * dx + (static_cast<double>(point.y) - a.y) * dy) /
        len2;
  }
  const double cx = static_cast<double>(a.x) + dx * t;
  const double cy = static_cast<double>(a.y) + dy * t;
  const double ex = static_cast<double>(point.x) - cx;
  const double ey = static_cast<double>(point.y) - cy;
  if (t_out != nullptr) {
    *t_out = t;
  }
  return std::sqrt(ex * ex + ey * ey);
}

Vec2 lerp(Vec2 a, Vec2 b, double t) {
  return {static_cast<float>(a.x + (b.x - a.x) * t),
          static_cast<float>(a.y + (b.y - a.y) * t)};
}

bool looks_like_window(std::string_view text) {
  const std::string lower = lower_ascii(text);
  if (lower.find("window") != std::string::npos) {
    return true;
  }
  if (text.find("窗") != std::string_view::npos) {
    return true;
  }
  // 国产图纸的编号约定：C1518 = 窗（1500×1800）、M0921 = 门（900×2100）。
  if (!lower.empty() && lower.front() == 'c') {
    for (std::size_t i = 1; i < lower.size(); ++i) {
      if (lower[i] >= '0' && lower[i] <= '9') {
        return true;
      }
    }
  }
  return false;
}

bool looks_like_door(std::string_view text) {
  const std::string lower = lower_ascii(text);
  if (lower.find("door") != std::string::npos) {
    return true;
  }
  if (text.find("门") != std::string_view::npos) {
    return true;
  }
  if (!lower.empty() && lower.front() == 'm') {
    for (std::size_t i = 1; i < lower.size(); ++i) {
      if (lower[i] >= '0' && lower[i] <= '9') {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

DrawingImportPlan build_drawing_import_plan(const Drawing& drawing,
                                            const DrawingImportOptions& options,
                                            const Grid* grid, DrawingImportInfo* info) {
  DrawingImportPlan plan;

  const double scale =
      options.unit_scale > 0.0 ? options.unit_scale : drawing.unit_scale_to_meter();
  const float elevation = static_cast<float>(options.elevation);

  // 图纸平面 (x, y) → 世界 (x, elevation, y)。
  // 相机注释写死了这条约定："Plan: screen X = world +X, screen up = world +Z (drawing Y)"；
  // 反过来（z = -y）会让平面图镜像。注意多边形轮廓喂给 OCCT 时要取负 y，
  // 因为 OCCT 是 Z-up、装配时按 (x,y,z)→(x,z,-y) 翻转。
  //
  // world_origin 是解析时为了浮点精度减掉的原点，这里要加回去：翻模结果落在**图纸
  // 绝对坐标**上，才能和用户照着这张图画的轴网对得上（图纸视图显示的也是绝对坐标）。
  const Vec2 origin = drawing.world_origin();
  const auto to_world = [scale, elevation, origin](Vec2 p) {
    return Vec3{static_cast<float>((static_cast<double>(p.x) + origin.x) * scale), elevation,
                static_cast<float>((static_cast<double>(p.y) + origin.y) * scale)};
  };
  // 吸附必须在**世界坐标**里做：轴网是按图纸绝对坐标画的，先吸附再平移就白吸了。
  const auto snap_world = [&](Vec3 world) {
    if (grid == nullptr || !options.align_to_grid || grid->empty()) {
      return world;
    }
    const Vec2 snapped = grid->snap_plan({world.x, world.z}, options.grid_snap_tolerance);
    world.x = snapped.x;
    world.z = snapped.y;
    return world;
  };

  const std::vector<std::string> wall_keywords = split_keywords(options.wall_layers);
  const std::vector<std::string> column_keywords = split_keywords(options.column_layers);
  const std::vector<std::string> opening_keywords = split_keywords(options.opening_layers);

  if (info != nullptr) {
    info->unit_scale = scale;
    info->unit_label = drawing.unit_label();
    info->layers.clear();
    for (const DrawingLayer& layer : drawing.layers()) {
      info->layers.push_back(layer.name);
    }
  }

  const auto layer_name = [&drawing](const DrawingPath& path) -> std::string_view {
    return path.layer < drawing.layers().size()
               ? std::string_view(drawing.layers()[path.layer].name)
               : std::string_view();
  };
  const auto is_opening_layer = [&](const DrawingPath& path) {
    if (matches_keywords(opening_keywords, layer_name(path))) {
      return true;
    }
    return !path.block.empty() &&
           (looks_like_door(path.block) || looks_like_window(path.block));
  };

  // ---- 墙：直线段（闭合多段线按每条边拆成一段墙）----
  std::size_t curved_segments = 0;
  for (const DrawingPath& path : drawing.paths()) {
    if (!path.block.empty()) {
      continue;  // 块内几何是门窗图例，不是墙
    }
    if (!matches_keywords(wall_keywords, layer_name(path))) {
      continue;
    }
    if (path.kind == DrawingPathKind::Circle || path.kind == DrawingPathKind::Arc ||
        path.kind == DrawingPathKind::Ellipse) {
      ++curved_segments;
      continue;
    }
    const std::size_t count = path.points.size();
    if (count < 2) {
      continue;
    }
    const std::size_t limit = path.closed ? count : count - 1;
    for (std::size_t i = 0; i < limit; ++i) {
      const Vec2 raw_a = path.points[i];
      const Vec2 raw_b = path.points[(i + 1) % count];
      const double length =
          std::sqrt(static_cast<double>(raw_b.x - raw_a.x) * (raw_b.x - raw_a.x) +
                    static_cast<double>(raw_b.y - raw_a.y) * (raw_b.y - raw_a.y)) *
          scale;
      if (length < options.min_wall_length) {
        // 曲线（圆弧按凸度离散）会被切成很多短段：它们是"没识别出来"，不是"没有墙"。
        if (path.kind == DrawingPathKind::Polyline || path.kind == DrawingPathKind::Arc) {
          ++curved_segments;
        }
        ++plan.skipped_segments;
        continue;
      }
      WallCandidate wall;
      wall.start = snap_world(to_world(raw_a));
      wall.end = snap_world(to_world(raw_b));
      wall.thickness = options.wall_thickness;
      wall.height = options.wall_height;
      wall.layer = std::string(layer_name(path));
      wall.confidence = 1.0f;
      plan.walls.push_back(std::move(wall));
    }
  }

  // ---- 柱：图上画圆 → 圆柱；画闭合小矩形 → 矩形柱 ----
  for (const DrawingPath& path : drawing.paths()) {
    if (!path.block.empty() || !matches_keywords(column_keywords, layer_name(path))) {
      continue;
    }
    const Bbox2 box = path_bounds(path);
    if (!box.valid()) {
      continue;
    }
    if (path.kind == DrawingPathKind::Circle) {
      const Vec2 center = box.center();
      double radius_sum = 0.0;
      for (const Vec2 p : path.points) {
        radius_sum += std::sqrt(static_cast<double>(p.x - center.x) * (p.x - center.x) +
                                static_cast<double>(p.y - center.y) * (p.y - center.y));
      }
      const double radius = path.points.empty()
                                ? 0.0
                                : radius_sum / static_cast<double>(path.points.size());
      const double diameter = 2.0 * radius * scale;
      if (diameter < options.min_column_size || diameter > options.max_column_size) {
        continue;
      }
      ColumnCandidate column;
      column.position = snap_world(to_world(center));
      column.circular = true;
      column.width = diameter;
      column.depth = diameter;
      column.height = options.column_height;
      column.layer = std::string(layer_name(path));
      column.confidence = 1.0f;
      plan.columns.push_back(std::move(column));
      continue;
    }
    if (path.closed && path.points.size() >= 4 && path.points.size() <= 8) {
      const double width = static_cast<double>(box.width()) * scale;
      const double depth = static_cast<double>(box.height()) * scale;
      if (width < options.min_column_size || depth < options.min_column_size ||
          width > options.max_column_size || depth > options.max_column_size) {
        continue;
      }
      ColumnCandidate column;
      column.position = snap_world(to_world(box.center()));
      column.circular = false;
      column.width = width;
      column.depth = depth;
      column.height = options.column_height;
      column.layer = std::string(layer_name(path));
      // 闭合四边形也可能是别的图例，置信度低一档，让用户复核。
      column.confidence = 0.7f;
      plan.columns.push_back(std::move(column));
    }
  }

  // ---- 门窗：按块名/图层聚成一簇，一簇 = 一樘 ----
  struct OpeningCluster {
    Bbox2 box;
    std::string block;
    std::string layer;
  };
  std::vector<OpeningCluster> clusters;
  const double merge_gap = 0.5 / (scale > 0.0 ? scale : 1.0);  // 图纸单位下的 0.5 米
  for (const DrawingPath& path : drawing.paths()) {
    if (!is_opening_layer(path)) {
      continue;
    }
    const Bbox2 box = path_bounds(path);
    if (!box.valid()) {
      continue;
    }
    OpeningCluster* target = nullptr;
    for (OpeningCluster& cluster : clusters) {
      if (cluster.block != path.block) {
        continue;
      }
      const bool overlaps = box.min_x - merge_gap <= cluster.box.max_x &&
                            box.max_x + merge_gap >= cluster.box.min_x &&
                            box.min_y - merge_gap <= cluster.box.max_y &&
                            box.max_y + merge_gap >= cluster.box.min_y;
      if (overlaps) {
        target = &cluster;
        break;
      }
    }
    if (target == nullptr) {
      OpeningCluster cluster;
      cluster.block = path.block;
      cluster.layer = std::string(layer_name(path));
      cluster.box = box;
      clusters.push_back(std::move(cluster));
      continue;
    }
    target->box.expand({box.min_x, box.min_y});
    target->box.expand({box.max_x, box.max_y});
  }

  for (const OpeningCluster& cluster : clusters) {
    const Vec2 center = cluster.box.center();
    const double span = std::max(cluster.box.width(), cluster.box.height()) * scale;
    const bool window = looks_like_window(cluster.block) ||
                        (!looks_like_door(cluster.block) && looks_like_window(cluster.layer));
    const double default_width = window ? options.window_width : options.door_width;
    const double width = span >= options.min_opening_width && span <= options.max_opening_width
                             ? span
                             : default_width;

    // 找宿主墙：中心到墙中线最近、且投影不超出墙端半米。
    // 墙体候选已经是米制世界坐标，直接在世界的 XZ 平面上比距离。
    const Vec3 center_world = to_world(center);
    const Vec2 c{center_world.x, center_world.z};
    std::size_t host = 0;
    double best = 1e30;
    Vec2 best_point = c;
    for (std::size_t i = 0; i < plan.walls.size(); ++i) {
      const WallCandidate& wall = plan.walls[i];
      const Vec2 a{wall.start.x, wall.start.z};
      const Vec2 b{wall.end.x, wall.end.z};
      double t = 0.0;
      const double distance = point_segment_distance(c, a, b, &t);
      const double length = std::sqrt(static_cast<double>(b.x - a.x) * (b.x - a.x) +
                                      static_cast<double>(b.y - a.y) * (b.y - a.y));
      const double overhang =
          length > kEps ? std::max(0.0, std::max(-t, t - 1.0) * length) : 1e30;
      if (overhang > 0.5) {
        continue;
      }
      const double limit = wall.thickness * 0.5 + options.host_tolerance;
      if (distance <= limit && distance < best) {
        best = distance;
        host = i;
        best_point = lerp(a, b, std::clamp(t, 0.0, 1.0));
      }
    }
    if (best > 1e29) {
      plan.warnings.push_back("门窗未能找到宿主墙，已跳过：" +
                              (cluster.block.empty() ? cluster.layer : cluster.block));
      continue;
    }

    OpeningCandidate opening;
    opening.door = !window;
    // 门窗不单独吸附：它必须待在宿主墙的中线上，位置由宿主决定。
    // best_point 已经是世界坐标（墙候选都是米制世界坐标），不要再换算一次。
    opening.position = Vec3{best_point.x, elevation, best_point.y};
    opening.width = width;
    opening.height = window ? options.window_height : options.door_height;
    opening.thickness = 0.05;
    opening.sill = window ? options.window_sill : 0.0;
    opening.host_wall = host;
    opening.layer = cluster.layer;
    opening.block = cluster.block;
    opening.confidence = cluster.block.empty() ? 0.6f : 1.0f;
    plan.openings.push_back(std::move(opening));
  }

  if (drawing.insunits() == 0) {
    plan.warnings.push_back("图纸没写单位（$INSUNITS），当前按米换算；如不对请在单位里改。");
  }
  if (curved_segments > 0) {
    plan.warnings.push_back("图纸里有圆弧/圆线段，翻模只处理直线墙，已跳过 " +
                            std::to_string(curved_segments) + " 段。");
  }
  if (!plan.empty()) {
    plan.warnings.push_back(
        "梁与板暂未识别：梁截面写在平法标注文字里、板需要闭合外轮廓，这两项留待后续版本。");
  }
  return plan;
}

DrawingImportPlan filter_drawing_import_plan(const DrawingImportPlan& plan,
                                             const std::vector<bool>& keep_walls,
                                             const std::vector<bool>& keep_columns,
                                             const std::vector<bool>& keep_openings,
                                             std::size_t* dropped_openings) {
  DrawingImportPlan out;
  out.warnings = plan.warnings;
  out.skipped_segments = plan.skipped_segments;

  const auto kept = [](const std::vector<bool>& flags, std::size_t index) {
    return index < flags.size() && flags[index];
  };

  std::vector<std::size_t> wall_remap(plan.walls.size(), 0);
  for (std::size_t i = 0; i < plan.walls.size(); ++i) {
    if (!kept(keep_walls, i)) {
      continue;
    }
    wall_remap[i] = out.walls.size();
    out.walls.push_back(plan.walls[i]);
  }
  for (std::size_t i = 0; i < plan.columns.size(); ++i) {
    if (kept(keep_columns, i)) {
      out.columns.push_back(plan.columns[i]);
    }
  }
  for (std::size_t i = 0; i < plan.openings.size(); ++i) {
    if (!kept(keep_openings, i)) {
      continue;
    }
    const OpeningCandidate& opening = plan.openings[i];
    if (opening.host_wall >= plan.walls.size() || !kept(keep_walls, opening.host_wall)) {
      if (dropped_openings != nullptr) {
        ++*dropped_openings;
      }
      continue;
    }
    OpeningCandidate copy = opening;
    copy.host_wall = wall_remap[opening.host_wall];
    out.openings.push_back(std::move(copy));
  }
  return out;
}

}  // namespace tamias
