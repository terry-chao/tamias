#pragma once

#include "engine/modeling/kernel/kernel.h"

#include <TopoDS_Shape.hxx>

#include <memory>
#include <string>

namespace tamias {

// OCCT 后端。全项目唯一允许 include BRep* / TopoDS* 做造型的地方（导入路径的
// occt_shape_ops 是同一层的兄弟）。
class OcctBody final : public Body {
 public:
  explicit OcctBody(const TopoDS_Shape& shape) : shape_(shape) {}

  [[nodiscard]] std::string backend_name() const override { return "occt"; }
  [[nodiscard]] const TopoDS_Shape& shape() const { return shape_; }

 private:
  TopoDS_Shape shape_;
};

class OcctKernel final : public ModelKernel {
 public:
  [[nodiscard]] KernelBackend backend() const override { return KernelBackend::Occt; }
  [[nodiscard]] std::string version() const override { return "OpenCASCADE"; }
  [[nodiscard]] KernelCapabilities capabilities() const override;

  [[nodiscard]] Result<BodyRef> make_rect_face(double width, double height) const override;
  [[nodiscard]] Result<BodyRef> make_circle_face(double radius) const override;
  [[nodiscard]] Result<BodyRef> make_polygon_face(std::span<const Vec3> loop) const override;
  [[nodiscard]] Result<BodyRef> extrude(const Body& profile, double depth) const override;
  [[nodiscard]] Result<BodyRef> boolean(const Body& a, const Body& b,
                                        BooleanOp op) const override;
  [[nodiscard]] Result<BodyRef> transform(const Body& body, Vec3 translation) const override;
  [[nodiscard]] Result<BodyRef> cylinder(double radius, double height, Vec3 center,
                                         Vec3 axis) const override;

  [[nodiscard]] Result<std::vector<EdgeId>> edges(const Body& body) const override;
  [[nodiscard]] Result<std::vector<EdgeMeasure>> measure_edges(const Body& body) const override;
  [[nodiscard]] Result<BodyRef> fillet(const Body& body, std::span<const EdgeId> edges,
                                       double radius) const override;
  [[nodiscard]] Result<BodyRef> chamfer(const Body& body, std::span<const EdgeId> edges,
                                        double distance) const override;

  [[nodiscard]] Result<MeshCpu> tessellate(const Body& body,
                                           double linear_deflection) const override;
  [[nodiscard]] Result<Aabb> bounds(const Body& body) const override;
  [[nodiscard]] const void* native_handle(const Body& body) const override;
};

// 注册进内核注册表（由 register_linked_kernels 调用）。
void register_occt_kernel_backend();

// 互操作逃逸口：拿到底层 TopoDS_Shape（IFC / STEP 导出这类）。不是 OcctBody 返回 nullptr。
[[nodiscard]] const TopoDS_Shape* occt_shape_of(const Body& body);

}  // namespace tamias
