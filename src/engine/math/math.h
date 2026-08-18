#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tamias {

struct Vec2 {
  float x = 0.f;
  float y = 0.f;
};

struct Vec3 {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;

  Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3& operator+=(Vec3 o) {
    x += o.x;
    y += o.y;
    z += o.z;
    return *this;
  }
};

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(Vec3 v) {
  const float len = length(v);
  return len > 1e-8f ? v * (1.f / len) : Vec3{};
}

struct Mat4 {
  float m[16]{};

  static Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
    return r;
  }

  float& operator()(int row, int col) { return m[col * 4 + row]; }
  float operator()(int row, int col) const { return m[col * 4 + row]; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
  Mat4 r{};
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      r(row, col) = a(row, 0) * b(0, col) + a(row, 1) * b(1, col) + a(row, 2) * b(2, col) +
                    a(row, 3) * b(3, col);
    }
  }
  return r;
}

// Affine transform of a point (column-major M; implicit w=1).
inline Vec3 operator*(const Mat4& m, Vec3 v) {
  return {m(0, 0) * v.x + m(0, 1) * v.y + m(0, 2) * v.z + m(0, 3),
          m(1, 0) * v.x + m(1, 1) * v.y + m(1, 2) * v.z + m(1, 3),
          m(2, 0) * v.x + m(2, 1) * v.y + m(2, 2) * v.z + m(2, 3)};
}

inline Mat4 translate(Vec3 t) {
  Mat4 r = Mat4::identity();
  r(0, 3) = t.x;
  r(1, 3) = t.y;
  r(2, 3) = t.z;
  return r;
}

inline Mat4 scale(Vec3 s) {
  Mat4 r{};
  r(0, 0) = s.x;
  r(1, 1) = s.y;
  r(2, 2) = s.z;
  r(3, 3) = 1.f;
  return r;
}

// 绕 Y 轴旋转（Y-up 世界的 yaw）。+Z 转到 (sin a, 0, cos a)。
inline Mat4 rotate_y(float angle) {
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  Mat4 r = Mat4::identity();
  r(0, 0) = c;
  r(0, 2) = s;
  r(2, 0) = -s;
  r(2, 2) = c;
  return r;
}

// Vulkan clip space: Y down in NDC after correction; we keep OpenGL-like math and
// let the device supply clipSpaceCorrectionMatrix().
inline Mat4 perspective(float fovy_rad, float aspect, float znear, float zfar) {
  const float f = 1.f / std::tan(fovy_rad * 0.5f);
  Mat4 r{};
  r(0, 0) = f / aspect;
  r(1, 1) = f;
  r(2, 2) = zfar / (znear - zfar);
  r(3, 2) = -1.f;
  r(2, 3) = (zfar * znear) / (znear - zfar);
  return r;
}

inline Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up) {
  const Vec3 f = normalize(center - eye);
  const Vec3 s = normalize(cross(f, up));
  const Vec3 u = cross(s, f);
  Mat4 r = Mat4::identity();
  r(0, 0) = s.x;
  r(0, 1) = s.y;
  r(0, 2) = s.z;
  r(1, 0) = u.x;
  r(1, 1) = u.y;
  r(1, 2) = u.z;
  r(2, 0) = -f.x;
  r(2, 1) = -f.y;
  r(2, 2) = -f.z;
  r(0, 3) = -dot(s, eye);
  r(1, 3) = -dot(u, eye);
  r(2, 3) = dot(f, eye);
  return r;
}

struct Aabb {
  Vec3 min{1e30f, 1e30f, 1e30f};
  Vec3 max{-1e30f, -1e30f, -1e30f};

  void expand(Vec3 p) {
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    min.z = std::min(min.z, p.z);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
    max.z = std::max(max.z, p.z);
  }

  [[nodiscard]] Vec3 center() const { return (min + max) * 0.5f; }
  [[nodiscard]] Vec3 extent() const { return max - min; }
  [[nodiscard]] bool valid() const { return min.x <= max.x; }
};

// World-space AABB of `box` under `m` (transforms all 8 corners, so it is exact
// under rotation/scale, not just translation).
inline Aabb transform_aabb(const Aabb& box, const Mat4& m) {
  const Vec3 corners[8] = {
      {box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z},
      {box.min.x, box.max.y, box.min.z}, {box.max.x, box.max.y, box.min.z},
      {box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z},
      {box.min.x, box.max.y, box.max.z}, {box.max.x, box.max.y, box.max.z},
  };
  Aabb out{};
  for (const auto& c : corners) {
    out.expand(m * c);
  }
  return out;
}

// World-space plane: n·p + d >= 0 is the inside half-space.
struct Plane {
  Vec3 n{};
  float d = 0.f;
};

// Camera frustum as six world-space planes (left, right, bottom, top, near, far).
// Extract from `proj * view` only — do not multiply clip-space correction
// (that matrix flips GPU NDC, not the world-space pyramid).
struct Frustum {
  Plane planes[6]{};

  static Frustum from_view_proj(const Mat4& view_proj);

  // Conservative: true if the box may be visible. Invalid boxes are kept
  // (never cull what we cannot bound).
  [[nodiscard]] bool intersects(const Aabb& box) const;
};

inline Frustum Frustum::from_view_proj(const Mat4& vp) {
  const auto make_plane = [](float a, float b, float c, float d) {
    Plane p{{a, b, c}, d};
    const float len = length(p.n);
    if (len > 1e-8f) {
      const float inv = 1.f / len;
      p.n = p.n * inv;
      p.d *= inv;
    }
    return p;
  };

  // Clip = vp * P. perspective() maps NDC z to [0, 1], xy to [-1, 1]:
  //   left/right/bottom/top: ±clip.w; near: clip.z >= 0; far: clip.z <= clip.w.
  const float r0[4] = {vp(0, 0), vp(0, 1), vp(0, 2), vp(0, 3)};
  const float r1[4] = {vp(1, 0), vp(1, 1), vp(1, 2), vp(1, 3)};
  const float r2[4] = {vp(2, 0), vp(2, 1), vp(2, 2), vp(2, 3)};
  const float r3[4] = {vp(3, 0), vp(3, 1), vp(3, 2), vp(3, 3)};

  Frustum f;
  f.planes[0] = make_plane(r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3]);
  f.planes[1] = make_plane(r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3]);
  f.planes[2] = make_plane(r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3]);
  f.planes[3] = make_plane(r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3]);
  f.planes[4] = make_plane(r2[0], r2[1], r2[2], r2[3]);
  f.planes[5] = make_plane(r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3]);
  return f;
}

inline bool Frustum::intersects(const Aabb& box) const {
  if (!box.valid()) {
    return true;
  }
  constexpr float kEps = 1e-3f;
  for (const Plane& plane : planes) {
    // p-vertex: AABB corner farthest in the +normal direction (inside).
    // If even that corner is outside, the whole box is outside.
    const Vec3 p{plane.n.x >= 0.f ? box.max.x : box.min.x,
                 plane.n.y >= 0.f ? box.max.y : box.min.y,
                 plane.n.z >= 0.f ? box.max.z : box.min.z};
    if (dot(plane.n, p) + plane.d < -kEps) {
      return false;
    }
  }
  return true;
}

struct Ray {
  Vec3 origin;
  Vec3 direction;
};

inline bool intersect_aabb(const Ray& ray, const Aabb& box, float& tmin_out) {
  float tmin = 0.f;
  float tmax = 1e30f;
  const float* o = &ray.origin.x;
  const float* d = &ray.direction.x;
  const float* bmin = &box.min.x;
  const float* bmax = &box.max.x;
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(d[i]) < 1e-8f) {
      if (o[i] < bmin[i] || o[i] > bmax[i]) {
        return false;
      }
      continue;
    }
    float inv = 1.f / d[i];
    float t0 = (bmin[i] - o[i]) * inv;
    float t1 = (bmax[i] - o[i]) * inv;
    if (t0 > t1) {
      std::swap(t0, t1);
    }
    tmin = std::max(tmin, t0);
    tmax = std::min(tmax, t1);
    if (tmax < tmin) {
      return false;
    }
  }
  tmin_out = tmin;
  return true;
}

inline bool intersect_triangle(const Ray& ray, Vec3 v0, Vec3 v1, Vec3 v2, float& t_out) {
  constexpr float kEps = 1e-6f;
  const Vec3 e1 = v1 - v0;
  const Vec3 e2 = v2 - v0;
  const Vec3 p = cross(ray.direction, e2);
  const float det = dot(e1, p);
  if (std::fabs(det) < kEps) {
    return false;
  }
  const float inv = 1.f / det;
  const Vec3 tvec = ray.origin - v0;
  const float u = dot(tvec, p) * inv;
  if (u < 0.f || u > 1.f) {
    return false;
  }
  const Vec3 q = cross(tvec, e1);
  const float v = dot(ray.direction, q) * inv;
  if (v < 0.f || u + v > 1.f) {
    return false;
  }
  const float t = dot(e2, q) * inv;
  if (t < kEps) {
    return false;
  }
  t_out = t;
  return true;
}

}  // namespace tamias
