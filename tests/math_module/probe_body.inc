// 共享探针计算体：由 header 侧和 module 侧各实例化一份。
//
// 走模板是为了让两侧实例化成不同的实体（HeaderTag / ModuleTag），避免同一个
// 函数名在“全局模块”和“具名模块”里各有一份定义而踩 ODR。本文件自身不 include
// 任何东西，调用方必须已经让 tamias 数学 API 可见。

template <class Tag>
std::array<float, kProbeCount> compute_math_probes() {
  using namespace tamias;

  std::array<float, kProbeCount> p{};
  std::size_t i = 0;
  auto push = [&](float v) { p.at(i++) = v; };

  push(dot(Vec3{1.f, 2.f, 3.f}, Vec3{4.f, 5.f, 6.f}));
  push(length(normalize(Vec3{3.f, 0.f, 4.f})));
  push(cross(Vec3{1.f, 0.f, 0.f}, Vec3{0.f, 1.f, 0.f}).z);

  const Mat4 model =
      translate(Vec3{1.f, 2.f, 3.f}) * rotate_y(0.5f) * scale(Vec3{2.f, 2.f, 2.f});
  const Vec3 world = model * Vec3{1.f, 0.f, 0.f};
  push(world.x);
  push(world.y);
  push(world.z);

  const Vec3 back = invert_affine(model) * world;
  push(std::fabs(back.x - 1.f) + std::fabs(back.y) + std::fabs(back.z));

  const Mat4 proj = perspective(0.8f, 16.f / 9.f, 0.1f, 500.f);
  push(proj(1, 1));
  push(proj(2, 2));

  const Mat4 view = look_at(Vec3{0.f, 0.f, 5.f}, Vec3{}, Vec3{0.f, 1.f, 0.f});
  push(view(2, 3));

  const Frustum frustum = Frustum::from_view_proj(proj * view);
  Aabb front;
  front.expand(Vec3{-1.f, -1.f, -1.f});
  front.expand(Vec3{1.f, 1.f, 1.f});
  push(frustum.intersects(front) ? 1.f : 0.f);
  Aabb behind;
  behind.expand(Vec3{-1.f, -1.f, 40.f});
  behind.expand(Vec3{1.f, 1.f, 42.f});
  push(frustum.intersects(behind) ? 1.f : 0.f);

  const Ray ray{Vec3{}, Vec3{0.f, 0.f, -1.f}};
  Aabb slab;
  slab.expand(Vec3{-1.f, -1.f, -5.f});
  slab.expand(Vec3{1.f, 1.f, -1.f});
  float t = 0.f;
  push(intersect_aabb(ray, slab, t) ? t : -1.f);
  push(intersect_triangle(ray, Vec3{-1.f, -1.f, -2.f}, Vec3{1.f, -1.f, -2.f},
                          Vec3{0.f, 1.f, -2.f}, t)
           ? t
           : -1.f);
  push(intersect_segment(ray, Vec3{0.f, 0.f, -3.f}, Vec3{0.f, 0.f, -3.f}, 0.1f, t) ? t
                                                                                    : -1.f);

  const Vec3 snapped = snap_to_grid_xz(Vec3{1.4f, 2.f, -2.6f});
  push(snapped.x);
  push(snapped.z);
  push(is_on_grid_xz(Vec3{2.f, 0.f, -3.f}) ? 1.f : 0.f);
  push(grid_snap_world_radius(10.f, 0.8f, 1080.f));

  TurntableCamera cam;
  cam.set_distance(5.f);
  cam.look_front();
  const Vec3 eye = cam.eye_position();
  push(eye.x);
  push(eye.y);
  push(eye.z);
  push(cam.view_matrix()(2, 2));

  while (i < kProbeCount) {
    p.at(i++) = std::numeric_limits<float>::quiet_NaN();
  }
  return p;
}
