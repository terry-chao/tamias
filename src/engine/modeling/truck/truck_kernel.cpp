#include "engine/modeling/truck/truck_kernel.h"

#include "engine/modeling/truck/truck_bridge.h"

#include <string>
#include <vector>

namespace tamias {
namespace {

std::string last_error() {
  const char* message = truck_last_error();
  return message == nullptr ? std::string("truck error") : std::string(message);
}

Result<BodyRef> wrap(std::int32_t code, std::uint64_t handle, const char* what) {
  if (code != TRUCK_OK) {
    if (code == TRUCK_E_UNSUPPORTED) {
      return Err(std::string("truck backend does not support ") + what);
    }
    return Err(std::string(what) + " failed: " + last_error());
  }
  return BodyRef{std::make_shared<TruckBody>(handle)};
}

const TruckBody* truck_body(const Body& body) { return dynamic_cast<const TruckBody*>(&body); }

}  // namespace

TruckBody::~TruckBody() {
  if (handle_ != 0) {
    (void)truck_body_release(handle_);
  }
}

std::string TruckKernel::version() const {
  const char* version = truck_version();
  return version == nullptr ? std::string("truck") : std::string(version);
}

KernelCapabilities TruckKernel::capabilities() const {
  KernelCapabilities caps;
  // 如实声明：Truck 0.6 没有布尔（在单独的 truck-shapeops 里）、没有圆角/倒角。
  // C++ 侧因此不会假装支持，UI 也能据此灰按钮。
  caps.verbs = KernelVerb::RectFace | KernelVerb::CircleFace | KernelVerb::PolygonFace |
               KernelVerb::Extrude | KernelVerb::Transform | KernelVerb::Edges |
               KernelVerb::MeasureEdges | KernelVerb::Tessellate | KernelVerb::Bounds;
  caps.multi_edge_fillet = false;
  caps.variable_radius_fillet = false;
  caps.step_import = false;
  caps.step_export = false;
  caps.native_brep_io = false;
  return caps;
}

Result<BodyRef> TruckKernel::make_rect_face(double width, double height) const {
  std::uint64_t handle = 0;
  // 注意：必须先调用再把 handle 取出来。写成 wrap(call(&handle), handle, ...) 时
  // C++ 的实参求值顺序未定义，handle 可能在调用之前就被读走（拿到 0）。
  const std::int32_t code = truck_rect_face(width, height, &handle);
  return wrap(code, handle, "make_rect_face");
}

Result<BodyRef> TruckKernel::make_circle_face(double radius) const {
  std::uint64_t handle = 0;
  const std::int32_t code = truck_circle_face(radius, &handle);
  return wrap(code, handle, "make_circle_face");
}

Result<BodyRef> TruckKernel::make_polygon_face(std::span<const Vec3> loop) const {
  if (loop.size() < 3) {
    return Err("make_polygon_face: needs at least 3 points");
  }
  std::vector<double> flat;
  flat.reserve(loop.size() * 3);
  for (const Vec3& p : loop) {
    flat.push_back(p.x);
    flat.push_back(p.y);
    flat.push_back(p.z);
  }
  std::uint64_t handle = 0;
  const std::int32_t code = truck_polygon_face(flat.data(), loop.size(), &handle);
  return wrap(code, handle, "make_polygon_face");
}

Result<BodyRef> TruckKernel::extrude(const Body& profile, double depth) const {
  const TruckBody* body = truck_body(profile);
  if (body == nullptr) {
    return Err("extrude: body is not a Truck body");
  }
  std::uint64_t handle = 0;
  const std::int32_t code = truck_extrude(body->handle(), depth, &handle);
  return wrap(code, handle, "extrude");
}

Result<BodyRef> TruckKernel::transform(const Body& body, Vec3 translation) const {
  const TruckBody* truck = truck_body(body);
  if (truck == nullptr) {
    return Err("transform: body is not a Truck body");
  }
  std::uint64_t handle = 0;
  const std::int32_t code =
      truck_transform(truck->handle(), translation.x, translation.y, translation.z, &handle);
  return wrap(code, handle, "transform");
}

Result<BodyRef> TruckKernel::boolean(const Body&, const Body&, BooleanOp) const {
  return Err("truck backend does not support boolean yet (truck-shapeops 未接入)");
}

Result<BodyRef> TruckKernel::cylinder(double, double, Vec3, Vec3) const {
  return Err("truck backend does not support cylinder yet");
}

Result<std::vector<EdgeId>> TruckKernel::edges(const Body& body) const {
  const TruckBody* truck = truck_body(body);
  if (truck == nullptr) {
    return Err("edges: body is not a Truck body");
  }
  std::size_t count = 0;
  if (const std::int32_t code = truck_edge_count(truck->handle(), &count); code != TRUCK_OK) {
    return Err("edges failed: " + last_error());
  }
  std::vector<EdgeId> ids(count);
  for (std::size_t i = 0; i < count; ++i) {
    ids[i] = static_cast<EdgeId>(i);
  }
  return ids;
}

Result<std::vector<EdgeMeasure>> TruckKernel::measure_edges(const Body& body) const {
  const TruckBody* truck = truck_body(body);
  if (truck == nullptr) {
    return Err("measure_edges: body is not a Truck body");
  }
  std::size_t count = 0;
  if (const std::int32_t code = truck_edge_count(truck->handle(), &count); code != TRUCK_OK) {
    return Err("measure_edges failed: " + last_error());
  }
  std::vector<TruckEdgeMeasure> raw(count);
  std::size_t written = 0;
  if (const std::int32_t code = truck_measure_edges(truck->handle(), raw.data(), count, &written);
      code != TRUCK_OK) {
    return Err("measure_edges failed: " + last_error());
  }
  std::vector<EdgeMeasure> out(written);
  for (std::size_t i = 0; i < written; ++i) {
    const TruckEdgeMeasure& in = raw[i];
    EdgeMeasure& m = out[i];
    m.key = in.key;
    m.mid = Vec3{static_cast<float>(in.mid[0]), static_cast<float>(in.mid[1]),
                 static_cast<float>(in.mid[2])};
    m.dir = Vec3{static_cast<float>(in.dir[0]), static_cast<float>(in.dir[1]),
                 static_cast<float>(in.dir[2])};
    m.has_dir = in.has_dir != 0;
    m.length = in.length;
    m.normal1 = Vec3{static_cast<float>(in.n1[0]), static_cast<float>(in.n1[1]),
                     static_cast<float>(in.n1[2])};
    m.has_normal1 = in.has_n1 != 0;
    m.normal2 = Vec3{static_cast<float>(in.n2[0]), static_cast<float>(in.n2[1]),
                     static_cast<float>(in.n2[2])};
    m.has_normal2 = in.has_n2 != 0;
  }
  return out;
}

Result<BodyRef> TruckKernel::fillet(const Body&, std::span<const EdgeId>, double) const {
  return Err("truck backend does not support fillet");
}

Result<BodyRef> TruckKernel::chamfer(const Body&, std::span<const EdgeId>, double) const {
  return Err("truck backend does not support chamfer");
}

Result<MeshCpu> TruckKernel::tessellate(const Body& body, double linear_deflection) const {
  const TruckBody* truck = truck_body(body);
  if (truck == nullptr) {
    return Err("tessellate: body is not a Truck body");
  }
  float* verts = nullptr;
  std::uint32_t* indices = nullptr;
  std::size_t vcount = 0;
  std::size_t icount = 0;
  const std::int32_t code = truck_tessellate(truck->handle(), linear_deflection, &verts,
                                             &vcount, &indices, &icount);
  if (code != TRUCK_OK) {
    return Err("tessellate failed: " + last_error());
  }
  MeshCpu mesh;
  mesh.vertices.resize(vcount);
  for (std::size_t i = 0; i < vcount; ++i) {
    Vertex& v = mesh.vertices[i];
    v.position = Vec3{verts[i * 6 + 0], verts[i * 6 + 1], verts[i * 6 + 2]};
    v.normal = Vec3{verts[i * 6 + 3], verts[i * 6 + 4], verts[i * 6 + 5]};
    v.color = Vec3{1.f, 1.f, 1.f};
  }
  mesh.indices.assign(indices, indices + icount);
  truck_verts_free(verts, vcount * 6);
  truck_indices_free(indices, icount);
  if (mesh.indices.empty()) {
    return Err("tessellate: truck produced an empty mesh");
  }
  recompute_bounds(mesh);
  return mesh;
}

Result<Aabb> TruckKernel::bounds(const Body& body) const {
  const TruckBody* truck = truck_body(body);
  if (truck == nullptr) {
    return Err("bounds: body is not a Truck body");
  }
  double lo[3] = {0.0, 0.0, 0.0};
  double hi[3] = {0.0, 0.0, 0.0};
  if (const std::int32_t code = truck_bounds(truck->handle(), lo, hi); code != TRUCK_OK) {
    return Err("bounds failed: " + last_error());
  }
  Aabb out{};
  out.expand(Vec3{static_cast<float>(lo[0]), static_cast<float>(lo[1]),
                  static_cast<float>(lo[2])});
  out.expand(Vec3{static_cast<float>(hi[0]), static_cast<float>(hi[1]),
                  static_cast<float>(hi[2])});
  return out;
}

void register_truck_kernel_backend() {
  register_kernel_backend(KernelModule{
      KernelBackend::Truck,
      [](const KernelCreateInfo&) -> Result<std::unique_ptr<ModelKernel>> {
        return std::make_unique<TruckKernel>();
      }});
}

}  // namespace tamias
