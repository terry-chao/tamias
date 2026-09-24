#include "engine/render/text/label_occluder.h"

namespace tamias {

bool LabelOccluder::try_reserve(float x, float y, float width, float height) {
  if (width <= 0.f || height <= 0.f) {
    return false;
  }
  const Rect candidate{x - padding_, y - padding_, x + width + padding_, y + height + padding_};
  for (const Rect& rect : rects_) {
    const bool overlaps = candidate.x0 < rect.x1 && rect.x0 < candidate.x1 &&
                          candidate.y0 < rect.y1 && rect.y0 < candidate.y1;
    if (overlaps) {
      return false;
    }
  }
  rects_.push_back(candidate);
  return true;
}

}  // namespace tamias
