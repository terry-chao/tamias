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

}  // namespace tamias
