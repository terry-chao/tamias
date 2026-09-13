#pragma once

#include "engine/core/result.h"
#include "engine/graphics/mesh.h"
#include "engine/math/math.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tamias {

// 建模内核的边界（对应渲染侧的 rhi/device.h）。
//
// 分层：
//   中间层（modeling：特征树遍历、草图数学、拓扑命名）只 include 这个头；
//   后端（occt/、将来的 acis/）实现这里的动词，是唯一允许 include BRep* / ACIS 头的地方。
//
// 接口刻意停在「BRep 操作」这一层，不停在「特征」这一层：后端不需要知道什么是
// Fillet 特征、参数叫什么、指纹怎么匹配，它只提供「把这条棱倒个 R 角」这种动作。
// 加新特征（旋转 / 抽壳 / 阵列）时，后端加一个动词，中间层加一个 case。

// feature.h 定义这个枚举（内核层不反向 include 中间层的数据头，只做不透明声明）。
enum class BooleanOp : std::uint8_t;

enum class KernelBackend { Occt };

[[nodiscard]] const char* to_string(KernelBackend backend);

struct KernelCreateInfo {
  KernelBackend backend = KernelBackend::Occt;
};

// 后端支持哪些动词。UI / 命令据此灰按钮，跨后端验收测试据此跳过，后端不必假装支持
// （对标 WebGL 后端没有线框 polygon mode）。
enum class KernelVerb : std::uint32_t {
  None = 0,
  RectFace = 1u << 0,
  CircleFace = 1u << 1,
  PolygonFace = 1u << 2,
  Extrude = 1u << 3,
  Boolean = 1u << 4,
  Transform = 1u << 5,
  Cylinder = 1u << 6,
  Edges = 1u << 7,
  MeasureEdges = 1u << 8,
  Tessellate = 1u << 9,
  Bounds = 1u << 10,
  Fillet = 1u << 11,
  Chamfer = 1u << 12,
};

inline KernelVerb operator|(KernelVerb a, KernelVerb b) {
  return static_cast<KernelVerb>(static_cast<std::uint32_t>(a) |
                                 static_cast<std::uint32_t>(b));
}

inline bool any(KernelVerb a, KernelVerb b) {
  return (static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b)) != 0;
}

struct KernelCapabilities {
  KernelVerb verbs = KernelVerb::None;
  bool multi_edge_fillet = false;  // 一次倒多条棱
  bool variable_radius_fillet = false;
  bool step_import = false;
  bool step_export = false;
  bool native_brep_io = false;  // BRep / IGES 之类原生读写

  [[nodiscard]] bool supports(KernelVerb verb) const { return any(verbs, verb); }
};

// 体：后端持有的不透明句柄。外面永远看不到 TopoDS_Shape / ENTITY*。
class Body {
 public:
  virtual ~Body() = default;
  [[nodiscard]] virtual std::string backend_name() const = 0;
};

// 共享句柄：同一个体可以被多个特征引用（布尔的两个输入、被下游复用的中间结果）。
using BodyRef = std::shared_ptr<const Body>;

// 边的标识：只保证「同一个 Body 的生命周期内」有效。
// 跨求值、跨会话的持久标识是中间层的事（几何指纹，见 edge_fingerprint.h），不是内核的事。
using EdgeId = std::uint32_t;

// 一条边量出来的几何（拓扑命名 / 拾取用）。坐标一律在 Tamias Y-up 局部空间，
// 这样不同后端的指纹可以互相比较。
struct EdgeMeasure {
  // 同一个体里「同一条几何边」的标识：后端给（OCCT 用 TShape+Location 的哈希）。
  // TopExp 会把同一条边按每个面的出现各枚举一次，这些重复项的 key 相同、EdgeId 不同。
  std::uint64_t key = 0;
  Vec3 mid{};  // 边上一点；闭合边（圆）取质心
  Vec3 dir{};  // 单位方向；闭合边没有稳定方向
  bool has_dir = false;
  double length = 0.0;
  Vec3 normal1{};  // 相邻两个面的法线（朝外）
  Vec3 normal2{};
  bool has_normal1 = false;
  bool has_normal2 = false;
};

class ModelKernel {
 public:
  virtual ~ModelKernel() = default;

  [[nodiscard]] virtual KernelBackend backend() const = 0;
  [[nodiscard]] virtual std::string version() const = 0;
  [[nodiscard]] virtual KernelCapabilities capabilities() const { return {}; }

  // —— 轮廓 / 体 ——
  // 轴对齐矩形轮廓面：宽 × 高，居中原点，落在 Tamias XZ 平面（y = 0）。
  [[nodiscard]] virtual Result<BodyRef> make_rect_face(double width, double height) const = 0;
  [[nodiscard]] virtual Result<BodyRef> make_circle_face(double radius) const = 0;
  // 默认多边形轮廓面：Tamias XZ 平面上的闭合点列（至少 3 点）。
  [[nodiscard]] virtual Result<BodyRef> make_polygon_face(std::span<const Vec3> loop) const = 0;
  // 沿 +Y（轮廓法线）拉伸成体。
  [[nodiscard]] virtual Result<BodyRef> extrude(const Body& profile, double depth) const = 0;
  [[nodiscard]] virtual Result<BodyRef> boolean(const Body& a, const Body& b,
                                                BooleanOp op) const = 0;
  [[nodiscard]] virtual Result<BodyRef> transform(const Body& body, Vec3 translation) const = 0;
  [[nodiscard]] virtual Result<BodyRef> cylinder(double radius, double height, Vec3 center,
                                                 Vec3 axis) const = 0;

  // —— 边 ——
  // 边的枚举顺序就是 EdgeId 的编号顺序：同一个 body 上必须稳定，且被 fillet /
  // measure_edge 共用（这是「第 N 条边」这个索引语义的落点）。
  [[nodiscard]] virtual Result<std::vector<EdgeId>> edges(const Body& body) const = 0;
  // 一次量完所有边（下标 = EdgeId）。拓扑命名要全文扫描，逐条查会变成 O(N²)。
  [[nodiscard]] virtual Result<std::vector<EdgeMeasure>> measure_edges(
      const Body& body) const = 0;
  [[nodiscard]] virtual Result<BodyRef> fillet(const Body& body, std::span<const EdgeId> edges,
                                               double radius) const = 0;
  [[nodiscard]] virtual Result<BodyRef> chamfer(const Body& body, std::span<const EdgeId> edges,
                                                double distance) const = 0;

  // —— 网格 / 查询 ——
  [[nodiscard]] virtual Result<MeshCpu> tessellate(const Body& body,
                                                   double linear_deflection) const = 0;
  [[nodiscard]] virtual Result<Aabb> bounds(const Body& body) const = 0;

  // 逃逸口：IFC / STEP 导出这类必须碰原生形状的互操作。只允许在明确的互操作层用。
  [[nodiscard]] virtual const void* native_handle(const Body&) const { return nullptr; }

  static Result<std::unique_ptr<ModelKernel>> create(const KernelCreateInfo& info);
};

struct KernelModule {
  KernelBackend backend{};
  std::function<Result<std::unique_ptr<ModelKernel>>(const KernelCreateInfo&)> create;
};

void register_kernel_backend(KernelModule module);
void clear_registered_kernel_backends();
[[nodiscard]] std::vector<KernelBackend> registered_kernel_backends();

}  // namespace tamias
