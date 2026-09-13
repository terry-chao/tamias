#pragma once

#include "engine/core/result.h"
#include "engine/graphics/mesh.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {

// 导入路径的内核边界：读文件 → Shape（懒离散）。
// 与 kernel.h 的 Body 是一对：Body 是参数化求值路径的句柄，Shape 是导入路径的
// 句柄（要长期持有、按需离散）。三条内核路线落地后两者合并，见 docs/MODELING-KERNEL.md。
class Shape {
 public:
  virtual ~Shape() = default;
  [[nodiscard]] virtual std::string backend_name() const = 0;
  [[nodiscard]] virtual Result<MeshCpu> tessellate(double linear_deflection = 0.1) const = 0;
  [[nodiscard]] virtual Aabb bounds() const = 0;
};

class IShapeOps {
 public:
  virtual ~IShapeOps() = default;
  [[nodiscard]] virtual std::string name() const = 0;
  [[nodiscard]] virtual Result<std::unique_ptr<Shape>> read_file(
      const std::filesystem::path& path) const = 0;
};

class ShapeOpsRegistry {
 public:
  static ShapeOpsRegistry& instance();
  void register_ops(std::unique_ptr<IShapeOps> ops);
  [[nodiscard]] IShapeOps* find(std::string_view name) const;
  [[nodiscard]] const std::vector<std::unique_ptr<IShapeOps>>& all() const { return ops_; }

 private:
  std::vector<std::unique_ptr<IShapeOps>> ops_;
};

// Placeholder mesh-backed shape used until OCCT arrives.
class MeshShape final : public Shape {
 public:
  explicit MeshShape(MeshCpu mesh) : mesh_(std::move(mesh)) {}
  [[nodiscard]] std::string backend_name() const override { return "mesh"; }
  [[nodiscard]] Result<MeshCpu> tessellate(double) const override { return mesh_; }
  [[nodiscard]] Aabb bounds() const override { return mesh_.bounds; }

 private:
  MeshCpu mesh_;
};

}  // namespace tamias
