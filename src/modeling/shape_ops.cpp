#include "shape_ops.h"

namespace tamias {

ShapeOpsRegistry& ShapeOpsRegistry::instance() {
  static ShapeOpsRegistry registry;
  return registry;
}

void ShapeOpsRegistry::register_ops(std::unique_ptr<IShapeOps> ops) {
  ops_.push_back(std::move(ops));
}

IShapeOps* ShapeOpsRegistry::find(std::string_view name) const {
  for (const auto& ops : ops_) {
    if (ops->name() == name) {
      return ops.get();
    }
  }
  return nullptr;
}

}  // namespace tamias
