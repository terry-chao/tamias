#pragma once

#include "engine/modeling/kernel/kernel.h"

#include <cstdint>
#include <string>

namespace tamias {

// Truck 后端（Rust）。C++ 这半边只做转接：Body 包一个 handle，动词转发给 C ABI。
class TruckBody final : public Body {
 public:
  explicit TruckBody(std::uint64_t handle) : handle_(handle) {}
  ~TruckBody() override;
  TruckBody(const TruckBody&) = delete;
  TruckBody& operator=(const TruckBody&) = delete;

  [[nodiscard]] std::string backend_name() const override { return "truck"; }
  [[nodiscard]] std::uint64_t handle() const { return handle_; }

 private:
  std::uint64_t handle_ = 0;
};

class TruckKernel final : public ModelKernel {
 public:
  [[nodiscard]] KernelBackend backend() const override { return KernelBackend::Truck; }
  [[nodiscard]] std::string version() const override;
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
};

// 注册进内核注册表（由 register_linked_kernels 调用）。
void register_truck_kernel_backend();

}  // namespace tamias
