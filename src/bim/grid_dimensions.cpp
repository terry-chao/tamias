#include "bim/grid_dimensions.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace tamias {

std::vector<GridDimension> grid_dimension_chain(const std::vector<GridAxis>& axes,
                                                GridAxisDirection direction) {
  std::vector<const GridAxis*> chain;
  chain.reserve(axes.size());
  for (const GridAxis& axis : axes) {
    if (axis.direction == direction) {
      chain.push_back(&axis);
    }
  }
  if (chain.size() < 2) {
    return {};
  }
  std::sort(chain.begin(), chain.end(), [](const GridAxis* a, const GridAxis* b) {
    return a->position < b->position;
  });

  // 链摆在轴网外缘：沿 z 的竖轴看 z 的最大端点，沿 x 的横轴看 x 的最大端点。
  double edge = -1e30;
  for (const GridAxis* axis : chain) {
    edge = (std::max)(edge, axis->end);
  }

  std::vector<GridDimension> out;
  out.reserve(chain.size() - 1);
  for (std::size_t i = 1; i < chain.size(); ++i) {
    const GridAxis& previous = *chain[i - 1];
    const GridAxis& current = *chain[i];
    const double middle = (previous.position + current.position) * 0.5;
    GridDimension dimension{};
    dimension.from_name = previous.name;
    dimension.to_name = current.name;
    dimension.distance = current.position - previous.position;
    if (direction == GridAxisDirection::AlongZ) {
      dimension.anchor = Vec3{static_cast<float>(middle), 0.f, static_cast<float>(edge)};
    } else {
      dimension.anchor = Vec3{static_cast<float>(edge), 0.f, static_cast<float>(middle)};
    }
    out.push_back(std::move(dimension));
  }
  return out;
}

}  // namespace tamias
