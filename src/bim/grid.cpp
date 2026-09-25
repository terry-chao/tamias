#include "bim/grid.h"

#include <algorithm>
#include <cmath>

namespace tamias {

GridAxis& Grid::add(GridAxis axis) {
  axis.id = next_id_++;
  axes_.push_back(std::move(axis));
  return axes_.back();
}

GridAxis& Grid::insert(GridAxis axis) {
  if (axis.id == 0) {
    axis.id = next_id_++;
  } else {
    next_id_ = std::max(next_id_, axis.id + 1);
  }
  axes_.push_back(std::move(axis));
  return axes_.back();
}

bool Grid::remove(std::uint64_t id) {
  for (auto it = axes_.begin(); it != axes_.end(); ++it) {
    if (it->id == id) {
      axes_.erase(it);
      return true;
    }
  }
  return false;
}

void Grid::clear() {
  axes_.clear();
  next_id_ = 1;
}

void Grid::select(std::uint64_t id) {
  if (GridAxis* axis = find(id)) {
    axis->selected = true;
  }
}

void Grid::deselect(std::uint64_t id) {
  if (GridAxis* axis = find(id)) {
    axis->selected = false;
  }
}

void Grid::clear_selection() {
  for (GridAxis& axis : axes_) {
    axis.selected = false;
  }
}

bool Grid::axis_selected(std::uint64_t id) const {
  const GridAxis* axis = find(id);
  return axis != nullptr && axis->selected;
}

bool Grid::has_selection() const {
  for (const GridAxis& axis : axes_) {
    if (axis.selected) {
      return true;
    }
  }
  return false;
}

std::vector<std::uint64_t> Grid::selected_ids() const {
  std::vector<std::uint64_t> ids;
  for (const GridAxis& axis : axes_) {
    if (axis.selected) {
      ids.push_back(axis.id);
    }
  }
  return ids;
}

GridAxis* Grid::find(std::uint64_t id) {
  for (GridAxis& axis : axes_) {
    if (axis.id == id) {
      return &axis;
    }
  }
  return nullptr;
}

const GridAxis* Grid::find(std::uint64_t id) const {
  for (const GridAxis& axis : axes_) {
    if (axis.id == id) {
      return &axis;
    }
  }
  return nullptr;
}

void Grid::replace(std::vector<GridAxis>& axes) {
  for (GridAxis& axis : axes) {
    if (axis.id == 0) {
      axis.id = next_id_++;
    } else {
      next_id_ = std::max(next_id_, axis.id + 1);
    }
  }
  axes_ = axes;  // 拷贝而非移动：调用方保留自己的表，redo 才能复用同一批 id
}

void Grid::sort_axes() {
  std::stable_sort(axes_.begin(), axes_.end(), [](const GridAxis& a, const GridAxis& b) {
    if (a.direction != b.direction) {
      return a.direction == GridAxisDirection::AlongZ;
    }
    return a.position < b.position;
  });
}

Aabb Grid::bounds() const {
  Aabb box;
  for (const GridAxis& axis : axes_) {
    box.expand(axis.start_point());
    box.expand(axis.end_point());
  }
  return box;
}

void Grid::append_segments(std::vector<Vec3>& out) const {
  append_axis_segments(axes_, out);
}

Vec2 Grid::snap_plan(Vec2 plan, double tolerance, bool* snapped) const {
  const double tol = std::max(tolerance, 0.0);
  // 两个方向各自找最近轴：x 与 z 的吸附互不干扰（否则只有更近的那个方向会贴上去）。
  double best_x = tol;
  double best_z = tol;
  Vec2 out = plan;
  bool hit = false;
  for (const GridAxis& axis : axes_) {
    if (axis.direction == GridAxisDirection::AlongZ) {
      const double d = std::fabs(static_cast<double>(plan.x) - axis.position);
      if (d <= best_x) {
        out.x = static_cast<float>(axis.position);
        best_x = d;
        hit = true;
      }
    } else {
      const double d = std::fabs(static_cast<double>(plan.y) - axis.position);
      if (d <= best_z) {
        out.y = static_cast<float>(axis.position);
        best_z = d;
        hit = true;
      }
    }
  }
  if (snapped != nullptr) {
    *snapped = hit;
  }
  return out;
}

namespace {

// 0 → A、25 → Z、26 → AA（超过 26 根字母轴时的进位）。
std::string grid_axis_letter(int index) {
  std::string name;
  int n = index;
  do {
    name.insert(name.begin(), static_cast<char>('A' + (n % 26)));
    n = n / 26 - 1;
  } while (n >= 0);
  return name;
}

}  // namespace

std::vector<GridAxis> make_orthogonal_grid(double origin_x, double origin_z,
                                           const std::vector<double>& x_spacings,
                                           const std::vector<double>& z_spacings,
                                           double margin) {
  std::vector<double> xs;
  std::vector<double> zs;
  if (!x_spacings.empty()) {
    xs.push_back(origin_x);
    for (const double s : x_spacings) {
      xs.push_back(xs.back() + s);
    }
  }
  if (!z_spacings.empty()) {
    zs.push_back(origin_z);
    for (const double s : z_spacings) {
      zs.push_back(zs.back() + s);
    }
  }

  // 轴线范围：某一侧没有轴就用另一侧的跨度兜住，保证轴线是完整网格而不是一根点。
  const double x_lo = xs.empty() ? origin_x : xs.front();
  const double x_hi = xs.empty() ? origin_x : xs.back();
  const double z_lo = zs.empty() ? origin_z : zs.front();
  const double z_hi = zs.empty() ? origin_z : zs.back();

  std::vector<GridAxis> axes;
  axes.reserve(xs.size() + zs.size());
  int number = 1;
  for (const double x : xs) {
    GridAxis axis;
    axis.name = std::to_string(number++);
    axis.direction = GridAxisDirection::AlongZ;
    axis.position = x;
    axis.start = z_lo - margin;
    axis.end = z_hi + margin;
    axes.push_back(std::move(axis));
  }
  int letter = 0;
  for (const double z : zs) {
    GridAxis axis;
    axis.name = grid_axis_letter(letter++);
    axis.direction = GridAxisDirection::AlongX;
    axis.position = z;
    axis.start = x_lo - margin;
    axis.end = x_hi + margin;
    axes.push_back(std::move(axis));
  }
  return axes;
}

void append_axis_segments(const std::vector<GridAxis>& axes, std::vector<Vec3>& out) {
  out.reserve(out.size() + axes.size() * 2);
  for (const GridAxis& axis : axes) {
    if (axis.length() <= 0.0) {
      continue;
    }
    out.push_back(axis.start_point());
    out.push_back(axis.end_point());
  }
}

std::vector<Vec3> grid_intersections(const std::vector<GridAxis>& axes,
                                     const std::vector<std::uint64_t>& ids, double tolerance) {
  const double tol = std::max(tolerance, 0.0);
  const auto wanted = [&ids](std::uint64_t id) {
    return ids.empty() || std::find(ids.begin(), ids.end(), id) != ids.end();
  };
  // 分两拨：AlongZ 定 x（编号轴），AlongX 定 z（字母轴）。只有两拨都有才谈得上交点。
  std::vector<const GridAxis*> numbered;
  std::vector<const GridAxis*> lettered;
  for (const GridAxis& axis : axes) {
    if (!wanted(axis.id) || axis.length() <= 0.0) {
      continue;
    }
    if (axis.direction == GridAxisDirection::AlongZ) {
      numbered.push_back(&axis);
    } else {
      lettered.push_back(&axis);
    }
  }

  std::vector<Vec3> points;
  points.reserve(numbered.size() * lettered.size());
  for (const GridAxis* x_axis : numbered) {
    for (const GridAxis* z_axis : lettered) {
      const double x = x_axis->position;
      const double z = z_axis->position;
      // 两个方向各自在自己的范围里：竖轴管 z 的区间，横轴管 x 的区间。
      if (x < z_axis->start - tol || x > z_axis->end + tol) {
        continue;
      }
      if (z < x_axis->start - tol || z > x_axis->end + tol) {
        continue;
      }
      const Vec3 point{static_cast<float>(x), 0.f, static_cast<float>(z)};
      const bool duplicate =
          std::any_of(points.begin(), points.end(), [&point, tol](const Vec3& other) {
            return std::fabs(static_cast<double>(other.x) - point.x) <= tol &&
                   std::fabs(static_cast<double>(other.z) - point.z) <= tol;
          });
      if (!duplicate) {
        points.push_back(point);
      }
    }
  }
  return points;
}

void translate_grid(std::vector<GridAxis>& axes, double dx, double dz) {
  for (GridAxis& axis : axes) {
    if (axis.direction == GridAxisDirection::AlongZ) {
      axis.position += dx;
      axis.start += dz;
      axis.end += dz;
    } else {
      axis.position += dz;
      axis.start += dx;
      axis.end += dx;
    }
  }
}

}  // namespace tamias
