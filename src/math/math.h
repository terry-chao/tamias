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
