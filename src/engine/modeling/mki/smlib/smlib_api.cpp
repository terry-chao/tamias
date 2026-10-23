// SMLib C 门面：唯一 include SMLib 头文件的 TU。
//
// 必须在 C++17 下编译（SMLib 头在 C++20+ 的 SmTreeVertex::operator== 上会歧义），
// 且必须定义 UNICODE/_UNICODE（发布包的 SMLib.dll 是 Unicode 版，TCHAR = wchar_t；
// 不定义的话 Dump/smos_* 这类符号的修饰名对不上，链接会失败）。
//
// 坐标：入口/出口都是 Tamias 的 Y-up；SMLib 的 Z-up 只在文件内部出现。

#include "smlib_c.h"

#include <SmApiBrep.h>
#include <SmApiCurves.h>
#include <SmApiGeneral.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <SmBSplineCurve.h>
#include <SmBrep.h>
#include <SmCircle.h>
#include <SmCurve.h>
#include <SmLine.h>
#include <SmPoly.h>
#include <SmVector3d.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tamias::smlib_c {
namespace {

std::mutex g_mutex;
std::once_flag g_init;
thread_local std::string g_error;

constexpr double kPi = 3.14159265358979323846;

int fail(const char* what, const std::string& detail) {
  g_error = std::string("smlib: ") + what + " failed: " + detail;
  return -1;
}

int fail_status(const char* what, long status) {
  return fail(what, "status " + std::to_string(status));
}

// ---- Tamias Y-up <-> SMLib Z-up -------------------------------------------
// (x, y, z) -> (x, -z, y)；与 OCCT 后端的做法一致（occt_kernel.cpp:56-75）。

SmPoint3d to_smlib(Vec3 p) { return SmPoint3d(p.x, -p.z, p.y); }
SmVector3d to_smlib_vec(Vec3 v) { return SmVector3d(v.x, -v.z, v.y); }

// SmPoint3d 是 SmVector3d 的 typedef，两个重载会撞成同一个签名。
Vec3 to_tamias(const SmVector3d& v) { return Vec3{v[0], v[2], -v[1]}; }

Vec3 subtract(Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(Vec3 v) { return std::sqrt(dot(v, v)); }
Vec3 normalized(Vec3 v) {
  const double len = length(v);
  return len > 1e-12 ? Vec3{v.x / len, v.y / len, v.z / len} : Vec3{0.0, 1.0, 0.0};
}

}  // namespace

// ---- 生命周期 ---------------------------------------------------------------

int init() {
  std::call_once(g_init, [] { SmApiCreateContext(); });
  return 0;
}

const char* last_error() { return g_error.c_str(); }

}  // namespace tamias::smlib_c

// 门面的对象定义（对 kernel 侧保持不透明）。
namespace tamias::smlib_c {

struct Brep {
  SmBrep* rep = nullptr;
  ~Brep() { delete rep; }
};

struct Profile {
  std::vector<SmCurve*> curves;
  ~Profile() {
    for (SmCurve* curve : curves) {
      delete curve;
    }
  }
};

struct Mesh {
  std::vector<float> positions;
  std::vector<float> normals;
};

namespace {

SmBrep* solid_of(const Brep* body) { return body == nullptr ? nullptr : body->rep; }

int wrap_brep(SmBrep* rep, Brep** out) {
  auto* body = new Brep;
  body->rep = rep;
  *out = body;
  return 0;
}

int copy_brep(SmBrep* src, SmBrep** out) {
  *out = nullptr;
  const long status = SmApiBrepCopy(src, *out);
  return status == SM_SUCCESS && *out != nullptr ? 0 : fail_status("SmApiBrepCopy", status);
}

}  // namespace

// ---- 轮廓 -------------------------------------------------------------------

int make_rect_profile(double width, double height, Profile** out) {
  if (out == nullptr) {
    return fail("make_rect_profile", "null output");
  }
  *out = nullptr;
  if (!(width > 0.0) || !(height > 0.0)) {
    return fail("make_rect_profile", "width and height must be positive");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    SmTArray<SmBSplineCurve*> curves;
    const long status = SmApiCreateRectangle(SmPoint3d(0.0, 0.0, 0.0), width, height, curves);
    if (status != SM_SUCCESS) {
      return fail_status("SmApiCreateRectangle", status);
    }
    auto* profile = new Profile;
    for (ULONG i = 0; i < curves.GetSize(); ++i) {
      profile->curves.push_back(curves[i]);
    }
    if (profile->curves.empty()) {
      delete profile;
      return fail("make_rect_profile", "no curves produced");
    }
    *out = profile;
    return 0;
  } catch (const std::exception& e) {
    return fail("make_rect_profile", e.what());
  } catch (...) {
    return fail("make_rect_profile", "unknown exception");
  }
}

int make_circle_profile(double radius, Profile** out) {
  if (out == nullptr) {
    return fail("make_circle_profile", "null output");
  }
  *out = nullptr;
  if (!(radius > 0.0)) {
    return fail("make_circle_profile", "radius must be positive");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    SmCircle* circle = nullptr;
    const long status = SmApiCreateCircle(SmPoint3d(0.0, 0.0, 0.0), radius, circle);
    if (status != SM_SUCCESS || circle == nullptr) {
      return fail_status("SmApiCreateCircle", status);
    }
    auto* profile = new Profile;
    profile->curves.push_back(circle);
    *out = profile;
    return 0;
  } catch (const std::exception& e) {
    return fail("make_circle_profile", e.what());
  } catch (...) {
    return fail("make_circle_profile", "unknown exception");
  }
}

int make_polygon_profile(const Vec3* points, std::size_t count, Profile** out) {
  if (out == nullptr) {
    return fail("make_polygon_profile", "null output");
  }
  *out = nullptr;
  if (points == nullptr || count < 3) {
    return fail("make_polygon_profile", "needs at least 3 points");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    std::vector<SmPoint3d> loop;
    loop.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      loop.push_back(to_smlib(points[i]));
    }
    // Tamias XZ -> SMLib XY 是 2D 反射，会把绕向翻过来；反序抵消，
    // 让用户在 Tamias 里的逆时针仍然是 SMLib 的逆时针。
    std::reverse(loop.begin(), loop.end());

    auto* profile = new Profile;
    for (std::size_t i = 0; i < loop.size(); ++i) {
      SmLine* line = nullptr;
      const long status =
          SmApiCreateLineSegment(loop[i], loop[(i + 1) % loop.size()], line);
      if (status != SM_SUCCESS || line == nullptr) {
        delete profile;
        return fail_status("SmApiCreateLineSegment", status);
      }
      profile->curves.push_back(line);
    }
    *out = profile;
    return 0;
  } catch (const std::exception& e) {
    return fail("make_polygon_profile", e.what());
  } catch (...) {
    return fail("make_polygon_profile", "unknown exception");
  }
}

void free_profile(Profile* profile) {
  if (profile == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  delete profile;
}

// ---- 体 ---------------------------------------------------------------------

int extrude(const Profile* profile, double depth, Brep** out) {
  if (out == nullptr) {
    return fail("extrude", "null output");
  }
  *out = nullptr;
  if (profile == nullptr || profile->curves.empty()) {
    return fail("extrude", "empty profile");
  }
  if (!(depth > 0.0)) {
    return fail("extrude", "depth must be positive");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    SmTArray<SmCurve*> curves;
    for (SmCurve* curve : profile->curves) {
      curves.Add(curve);
    }
    SmBrep* solid = nullptr;
    const long status =
        SmApiCreateLinearSweep(curves, SmVector3d(0.0, 0.0, 1.0), depth, TRUE, solid);
    if (status != SM_SUCCESS || solid == nullptr) {
      return fail_status("SmApiCreateLinearSweep", status);
    }
    return wrap_brep(solid, out);
  } catch (const std::exception& e) {
    return fail("extrude", e.what());
  } catch (...) {
    return fail("extrude", "unknown exception");
  }
}

int boolean(const Brep* a, const Brep* b, int op, Brep** out) {
  if (out == nullptr) {
    return fail("boolean", "null output");
  }
  *out = nullptr;
  if (a == nullptr || b == nullptr) {
    return fail("boolean", "null operand");
  }
  if (op < 0 || op > 2) {
    return fail("boolean", "unknown operation");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    SmBrep* lhs = nullptr;
    SmBrep* rhs = nullptr;
    if (const int rc = copy_brep(a->rep, &lhs); rc != 0) {
      return rc;
    }
    if (const int rc = copy_brep(b->rep, &rhs); rc != 0) {
      delete lhs;
      return rc;
    }
    SmBrep* result = nullptr;
    long status = SM_ERR;
    if (op == 0) {
      status = SmApiBooleanUnion(lhs, rhs, result);
    } else if (op == 1) {
      status = SmApiBooleanIntersection(lhs, rhs, result);
    } else {
      status = SmApiBooleanDifference(lhs, rhs, result);
    }
    if (status != SM_SUCCESS || result == nullptr) {
      delete lhs;
      delete rhs;
      return fail_status("SmApiBoolean", status);
    }
    // 成功：result 就是 lhs，rhs 已被 API 删除。
    return wrap_brep(result, out);
  } catch (const std::exception& e) {
    return fail("boolean", e.what());
  } catch (...) {
    return fail("boolean", "unknown exception");
  }
}

int transform(const Brep* body, Vec3 translation, Brep** out) {
  if (out == nullptr) {
    return fail("transform", "null output");
  }
  *out = nullptr;
  if (body == nullptr) {
    return fail("transform", "null body");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    SmBrep* copy = nullptr;
    if (const int rc = copy_brep(body->rep, &copy); rc != 0) {
      return rc;
    }
    const long status = SmApiTranslate(copy, to_smlib_vec(translation));
    if (status != SM_SUCCESS) {
      delete copy;
      return fail_status("SmApiTranslate", status);
    }
    return wrap_brep(copy, out);
  } catch (const std::exception& e) {
    return fail("transform", e.what());
  } catch (...) {
    return fail("transform", "unknown exception");
  }
}

int cylinder(double radius, double height, Vec3 center, Vec3 axis, Brep** out) {
  if (out == nullptr) {
    return fail("cylinder", "null output");
  }
  *out = nullptr;
  if (!(radius > 0.0) || !(height > 0.0)) {
    return fail("cylinder", "radius and height must be positive");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    Vec3 safe_axis = length(axis) > 1e-9 ? normalized(axis) : Vec3{0.0, 1.0, 0.0};
    const Vec3 base = Vec3{center.x - safe_axis.x * height * 0.5,
                           center.y - safe_axis.y * height * 0.5,
                           center.z - safe_axis.z * height * 0.5};
    SmBrep* solid = nullptr;
    const long status = SmApiCreateCylinder(to_smlib(base), radius, height, solid);
    if (status != SM_SUCCESS || solid == nullptr) {
      return fail_status("SmApiCreateCylinder", status);
    }

    // SMLib 的圆柱沿 +Z 从底面长；Tamias 的 axis 是 Y-up 方向。
    const Vec3 sm_axis = normalized(Vec3{safe_axis.x, -safe_axis.z, safe_axis.y});
    const double cos_angle = std::clamp(sm_axis.z, -1.0, 1.0);
    if (cos_angle < 1.0 - 1e-9) {
      const SmVector3d pivot = to_smlib(base);
      if (cos_angle < -1.0 + 1e-9) {
        // 完全反向：绕 X 轴转 180°。
        const long rotate = SmApiRotate(solid, pivot, SmVector3d(1.0, 0.0, 0.0), 180.0);
        if (rotate != SM_SUCCESS) {
          delete solid;
          return fail_status("SmApiRotate", rotate);
        }
      } else {
        const SmVector3d rot_axis(-sm_axis.y, sm_axis.x, 0.0);
        const double angle_deg = std::acos(cos_angle) * 180.0 / kPi;
        const long rotate = SmApiRotate(solid, pivot, rot_axis, angle_deg);
        if (rotate != SM_SUCCESS) {
          delete solid;
          return fail_status("SmApiRotate", rotate);
        }
      }
    }
    return wrap_brep(solid, out);
  } catch (const std::exception& e) {
    return fail("cylinder", e.what());
  } catch (...) {
    return fail("cylinder", "unknown exception");
  }
}

namespace {

int fillet_or_chamfer(const char* what, const Brep* body, const std::uint32_t* edge_ids,
                      std::size_t count, double size, ULONG xsec, Brep** out) {
  if (out == nullptr) {
    return fail(what, "null output");
  }
  *out = nullptr;
  if (body == nullptr) {
    return fail(what, "null body");
  }
  if (edge_ids == nullptr || count == 0) {
    return fail(what, "no edges");
  }
  if (!(size > 0.0)) {
    return fail(what, "size must be positive");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    SmBrep* copy = nullptr;
    if (const int rc = copy_brep(body->rep, &copy); rc != 0) {
      return rc;
    }
    SmTArray<SmEdge*> all;
    const long status = SmApiGetEdges(copy, all);
    if (status != SM_SUCCESS) {
      delete copy;
      return fail_status("SmApiGetEdges", status);
    }
    SmTArray<SmEdge*> selected;
    for (std::size_t i = 0; i < count; ++i) {
      if (edge_ids[i] >= all.GetSize()) {
        delete copy;
        return fail(what, "edge id out of range");
      }
      selected.Add(all[edge_ids[i]]);
    }
    const long result = SmApiFilletEdges(copy, selected, size, xsec, 1, 1.0);
    if (result != SM_SUCCESS) {
      delete copy;
      return fail_status("SmApiFilletEdges", result);
    }
    return wrap_brep(copy, out);
  } catch (const std::exception& e) {
    return fail(what, e.what());
  } catch (...) {
    return fail(what, "unknown exception");
  }
}

}  // namespace

int fillet(const Brep* body, const std::uint32_t* edge_ids, std::size_t count, double radius,
           Brep** out) {
  return fillet_or_chamfer("fillet", body, edge_ids, count, radius, 1, out);
}

int chamfer(const Brep* body, const std::uint32_t* edge_ids, std::size_t count, double distance,
            Brep** out) {
  return fillet_or_chamfer("chamfer", body, edge_ids, count, distance, 0, out);
}

// ---- 查询 -------------------------------------------------------------------

int edge_count(const Brep* body, std::size_t* out) {
  if (out == nullptr) {
    return fail("edge_count", "null output");
  }
  *out = 0;
  if (body == nullptr) {
    return fail("edge_count", "null body");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    SmTArray<SmEdge*> edges;
    const long status = SmApiGetEdges(body->rep, edges);
    if (status != SM_SUCCESS) {
      return fail_status("SmApiGetEdges", status);
    }
    *out = edges.GetSize();
    return 0;
  } catch (const std::exception& e) {
    return fail("edge_count", e.what());
  } catch (...) {
    return fail("edge_count", "unknown exception");
  }
}

int measure_edges(const Brep* body, EdgeMeasure** out, std::size_t* count) {
  if (out == nullptr || count == nullptr) {
    return fail("measure_edges", "null output");
  }
  *out = nullptr;
  *count = 0;
  if (body == nullptr) {
    return fail("measure_edges", "null body");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    SmTArray<SmEdge*> edges;
    const long status = SmApiGetEdges(body->rep, edges);
    if (status != SM_SUCCESS) {
      return fail_status("SmApiGetEdges", status);
    }
    const std::size_t n = edges.GetSize();
    auto* measures = new EdgeMeasure[n]();
    for (std::size_t i = 0; i < n; ++i) {
      EdgeMeasure& m = measures[i];
      m.key = static_cast<std::uint64_t>(i) + 1;
      SmEdge* edge = edges[i];
      if (edge == nullptr) {
        continue;
      }
      double u0 = 0.0;
      double u1 = 0.0;
      if (SmApiEdgeParameterRange(edge, u0, u1) == SM_SUCCESS) {
        const double mid = 0.5 * (u0 + u1);
        SmVector3d tangent;
        if (SmApiEdgeTangent(edge, mid, tangent) == SM_SUCCESS) {
          m.dir = to_tamias(tangent);
          m.has_dir = length(m.dir) > 1e-9;
        }
        SmCurve* curve = edge->GetCurve();
        SmPoint3d point;
        if (curve != nullptr && curve->EvaluatePoint(mid, point) == SM_SUCCESS) {
          m.mid = to_tamias(point);
        }
      }
      double edge_length = 0.0;
      if (SmApiEdgeComputeLength(edge, 1e-6, edge_length) == SM_SUCCESS) {
        m.length = edge_length;
      }
    }
    *out = measures;
    *count = n;
    return 0;
  } catch (const std::exception& e) {
    return fail("measure_edges", e.what());
  } catch (...) {
    return fail("measure_edges", "unknown exception");
  }
}

void free_edge_measures(EdgeMeasure* measures) { delete[] measures; }

int tessellate(const Brep* body, double deflection, Mesh** out) {
  if (out == nullptr) {
    return fail("tessellate", "null output");
  }
  *out = nullptr;
  if (body == nullptr) {
    return fail("tessellate", "null body");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    SmTessellationParams params;
    params.dChordHeightTol = deflection > 0.0 ? deflection : 0.1;
    params.dCurveAngleTolDeg = 20.0;
    params.dSurfaceAngleTolDeg = 20.0;
    SmTArray<SmTessellationFailure> failures;
    SmPolyBrep* poly = nullptr;
    const long status = SmApiTessellate(body->rep, poly, params, TRUE, &failures);
    if (poly == nullptr) {
      return fail_status("SmApiTessellate", status);
    }
    std::unique_ptr<SmPolyBrep> owned(poly);

    auto* mesh = new Mesh;
    SmTArray<SmPolyFace*> faces;
    poly->GetPolyFaces(faces);
    std::vector<Vec3> loop;
    for (ULONG f = 0; f < faces.GetSize(); ++f) {
      SmPolyFace* face = faces[f];
      if (face == nullptr) {
        continue;
      }
      SmTArray<SmPolyVertex*> vertices;
      face->GetPolyVertices(vertices);
      const ULONG n = vertices.GetSize();
      if (n < 3) {
        continue;
      }
      const SmVector3d sm_normal = face->GetNormal(FALSE, FALSE);
      Vec3 normal = to_tamias(sm_normal);
      if (length(normal) < 1e-9) {
        normal = Vec3{0.0, 1.0, 0.0};
      } else {
        normal = normalized(normal);
      }
      loop.clear();
      loop.reserve(n);
      for (ULONG i = 0; i < n; ++i) {
        loop.push_back(to_tamias(vertices[i]->GetPoint()));
      }
      for (std::size_t i = 1; i + 1 < loop.size(); ++i) {
        Vec3 a = loop[0];
        Vec3 b = loop[i];
        Vec3 c = loop[i + 1];
        const Vec3 cr = cross(subtract(b, a), subtract(c, a));
        if (dot(cr, normal) < 0.0) {
          std::swap(b, c);
        }
        const Vec3 tri[3] = {a, b, c};
        for (const Vec3& p : tri) {
          mesh->positions.push_back(static_cast<float>(p.x));
          mesh->positions.push_back(static_cast<float>(p.y));
          mesh->positions.push_back(static_cast<float>(p.z));
          mesh->normals.push_back(static_cast<float>(normal.x));
          mesh->normals.push_back(static_cast<float>(normal.y));
          mesh->normals.push_back(static_cast<float>(normal.z));
        }
      }
    }
    if (mesh->positions.empty()) {
      delete mesh;
      return fail("tessellate", "empty mesh");
    }
    *out = mesh;
    return 0;
  } catch (const std::exception& e) {
    return fail("tessellate", e.what());
  } catch (...) {
    return fail("tessellate", "unknown exception");
  }
}

int mesh_view(const Mesh* mesh, MeshView* out) {
  if (mesh == nullptr || out == nullptr) {
    return fail("mesh_view", "null argument");
  }
  out->positions = mesh->positions.data();
  out->normals = mesh->normals.data();
  out->vertex_count = mesh->positions.size() / 3;
  return 0;
}

void free_mesh(Mesh* mesh) {
  if (mesh == nullptr) {
    return;
  }
  // SmPolyBrep 由 SMLib 的 context 分配/回收，delete 也算内核调用，和别的调用串行。
  std::lock_guard<std::mutex> lock(g_mutex);
  delete mesh;
}

int bounds(const Brep* body, Bounds* out) {
  if (out == nullptr) {
    return fail("bounds", "null output");
  }
  if (body == nullptr) {
    return fail("bounds", "null body");
  }
  init();
  std::lock_guard<std::mutex> lock(g_mutex);
  try {
    SmPoint3d lo;
    SmPoint3d hi;
    const long status = SmApiBrepBoundingBox(body->rep, FALSE, lo, hi);
    if (status != SM_SUCCESS) {
      return fail_status("SmApiBrepBoundingBox", status);
    }
    const Vec3 a = to_tamias(lo);
    const Vec3 b = to_tamias(hi);
    out->min[0] = std::min(a.x, b.x);
    out->min[1] = std::min(a.y, b.y);
    out->min[2] = std::min(a.z, b.z);
    out->max[0] = std::max(a.x, b.x);
    out->max[1] = std::max(a.y, b.y);
    out->max[2] = std::max(a.z, b.z);
    return 0;
  } catch (const std::exception& e) {
    return fail("bounds", e.what());
  } catch (...) {
    return fail("bounds", "unknown exception");
  }
}

void free_brep(Brep* body) {
  if (body == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  delete body;
}

}  // namespace tamias::smlib_c
