#include "engine/drawing/drawing.h"

#include <utility>

namespace tamias {

void Drawing::expand_bounds(Vec2 p) {
  bounds_.expand(p.x, p.y);
}

void Drawing::add_path(DrawingPath path) {
  for (const Vec2 p : path.points) {
    expand_bounds(p);
  }
  paths_.push_back(std::move(path));
}

void Drawing::add_text(DrawingText text) {
  expand_bounds(text.position);
  texts_.push_back(std::move(text));
}

std::uint32_t Drawing::ensure_layer(std::string_view name, Vec3 color) {
  const std::string key(name);
  if (const auto it = layer_lookup_.find(key); it != layer_lookup_.end()) {
    return it->second;
  }
  const auto index = static_cast<std::uint32_t>(layers_.size());
  DrawingLayer layer;
  layer.name = key;
  layer.color = color;
  layers_.push_back(std::move(layer));
  layer_lookup_.emplace(key, index);
  return index;
}

void Drawing::normalize_origin() {
  if (!bounds_.valid()) {
    return;
  }
  const Vec2 offset{bounds_.min_x, bounds_.min_y};
  if (offset.x == 0.f && offset.y == 0.f) {
    return;
  }
  for (DrawingPath& path : paths_) {
    for (Vec2& p : path.points) {
      p.x -= offset.x;
      p.y -= offset.y;
    }
  }
  for (DrawingText& text : texts_) {
    text.position.x -= offset.x;
    text.position.y -= offset.y;
  }
  bounds_.min_x -= offset.x;
  bounds_.min_y -= offset.y;
  bounds_.max_x -= offset.x;
  bounds_.max_y -= offset.y;
  world_origin_.x += offset.x;
  world_origin_.y += offset.y;
}

// AutoCAD $INSUNITS 编号。翻模只需要常见几种，其余归为"未知，按米算"。
double Drawing::unit_scale_to_meter() const {
  switch (insunits_) {
    case 1:   // inches
      return 0.0254;
    case 2:   // feet
      return 0.3048;
    case 4:   // millimetres
      return 0.001;
    case 5:   // centimetres
      return 0.01;
    case 6:   // metres
      return 1.0;
    case 7:   // kilometres
      return 1000.0;
    case 10:  // yards
      return 0.9144;
    case 14:  // decimetres
      return 0.1;
    default:
      return 1.0;
  }
}

const char* Drawing::unit_label() const {
  switch (insunits_) {
    case 1:
      return "in";
    case 2:
      return "ft";
    case 4:
      return "mm";
    case 5:
      return "cm";
    case 6:
      return "m";
    case 7:
      return "km";
    case 10:
      return "yd";
    case 14:
      return "dm";
    default:
      return "unit";
  }
}

}  // namespace tamias
