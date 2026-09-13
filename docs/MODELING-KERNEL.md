# 建模内核（Kernel）

> 造型层的中间层与后端怎么分：中间层（特征树、求值器、草图数学、拓扑命名）**不含任何内核类型**；
> 后端（OCCT，将来的 ACIS / Truck）只实现一组「BRep 动词」。结构照抄渲染侧的 RHI。

代码在 [`src/engine/modeling/`](https://github.com/terry-chao/tamias/tree/main/src/engine/modeling)。

---

## 1. 为什么要有这一层

改造前，`occt_feature.cpp` 一个文件里同时装着：特征树怎么遍历、参数怎么解析、指纹怎么匹配、
以及所有 OCCT 调用。想换内核（或者只是想给 WASM 编译一个不带 OCCT 的查看器）时，
没有任何缝可以下刀。

现在的边界是：

```
Qt / WASM 壳
      │
中间层（内核无关）  feature.h · evaluator · curve_geom · edge_fingerprint · tess_worker
      │  只 include kernel/kernel.h
内核接口            ModelingKernel 接口（动词 + 能力 + 注册表）
      │
后端                occt/   （将来 acis/）
                   唯一允许 include BRep* / ENTITY* 的地方
```

和 RHI 一一对应：

| RHI | 建模内核 |
|---|---|
| `rhi/device.h` 定义动词 | `kernel/kernel.h` 定义动词 |
| `BackendModule` + `register_backend()` | `KernelModule` + `register_kernel_backend()` |
| `RHIDevice::create({backend})` | `ModelKernel::create({backend})` |
| `Buffer` / `Texture` 不透明句柄 | `Body` 不透明句柄 + `EdgeId` |
| `rhi/vulkan/`、`rhi/opengl/` | `modeling/occt/`、将来的 `modeling/acis/` |
| `register_linked_rhi_backends()` | `register_linked_kernels()` |
| 后端能力不同就降级（WebGL 没线框） | `KernelCapabilities`，UI 按能力灰按钮 |

---

## 2. 接口刻意停在哪一层

**停在「BRep 操作」层，不停在「特征」层。** 后端不需要知道什么是 `Fillet` 特征、参数叫什么、
指纹怎么匹配；它只提供「把这条棱倒个 R 角」这种动作。于是：

- 加新特征（旋转 / 抽壳 / 阵列）：后端加一个动词，中间层加一个 `case`；
- 换后端：只写动词实现，特征树、参数、撤销、拓扑命名、`.tdoc` 全都不用动；
- 同一份 `.tdoc` 可以被不同后端求值。

```cpp
class ModelKernel {
 public:
  // 轮廓 / 体
  virtual Result<BodyRef> make_rect_face(double width, double height) const = 0;
  virtual Result<BodyRef> make_circle_face(double radius) const = 0;
  virtual Result<BodyRef> make_polygon_face(std::span<const Vec3> loop) const = 0;
  virtual Result<BodyRef> extrude(const Body& profile, double depth) const = 0;
  virtual Result<BodyRef> boolean(const Body& a, const Body& b, BooleanOp op) const = 0;
  virtual Result<BodyRef> transform(const Body& body, Vec3 translation) const = 0;
  virtual Result<BodyRef> cylinder(double radius, double height, Vec3 center, Vec3 axis) const = 0;
  // 边：枚举顺序 = EdgeId 编号顺序，fillet / measure_edges 共用
  virtual Result<std::vector<EdgeId>> edges(const Body& body) const = 0;
  virtual Result<std::vector<EdgeMeasure>> measure_edges(const Body& body) const = 0;
  virtual Result<BodyRef> fillet(const Body&, std::span<const EdgeId>, double radius) const = 0;
  virtual Result<BodyRef> chamfer(const Body&, std::span<const EdgeId>, double distance) const = 0;
  // 网格 / 查询
  virtual Result<MeshCpu> tessellate(const Body&, double deflection) const = 0;
  virtual Result<Aabb> bounds(const Body&) const = 0;
  // 逃逸口：IFC / STEP 导出这类互操作，只允许在明确的互操作层用
  virtual const void* native_handle(const Body&) const { return nullptr; }
};
```

几条规则：

1. **`Body` 不透明、`shared_ptr` 持有**：外面永远看不到 `TopoDS_Shape`。
2. **`EdgeId` 只在一个 `Body` 的生命周期内有效**。跨求值、跨会话的持久标识是中间层的事
   （几何指纹），不是内核的事。
3. **`EdgeMeasure` 一律用 Tamias Y-up 局部坐标**，后端负责换算。这样不同后端量出来的指纹
   可以直接比较。
4. **`measure_edges` 是批量的**（下标 = `EdgeId`）。拓扑命名要全量扫描，逐条查会变成 O(N²)。
5. **能力查询而不是假装支持**：`KernelCapabilities` 说明多边倒角、变半径、STEP 读写这些
   支持到哪一步，UI / 命令据此灰按钮。

---

## 3. 目录

```
src/engine/modeling/
  feature.h  curve_geom.*  curve_kind.h       纯数据 / 纯数学，内核无关
  kernel/                                     接口层（≈ render/rhi）
    kernel.h            ModelKernel / Body / EdgeId / EdgeMeasure / 能力 / 注册表
    kernel_registry.cpp register_kernel_backend / ModelKernel::create
    shape_ops.h/.cpp    导入路径的边界（Shape / IShapeOps）
  evaluator.h/.cpp                            中间层：特征树 → 体 → 三角网
  edge_fingerprint.h/.cpp                     中间层：指纹存储 + 匹配（索引 + 几何指纹）
  linked_kernels.cpp                          把编译进来的后端注册进注册表
  geom_builder.h/.cpp                         过渡 shim（老调用方要的 IGeometryBuilder）
  tess_worker.*                               后台离散队列
  occt/                                       后端 1：唯一 include BRep* 的地方
    occt_kernel.h/.cpp   动词实现 + 边的测量
    occt_shape_ops.h/.cpp 导入路径（STEP / IGES / BREP + XCAF 颜色）
```

CMake 也是同一形状：`tamias::kernel`（接口）← `tamias::modeling`（中间层）← `tamias::kernel_occt`（后端），
中间层通过 `TAMIAS_HAS_KERNEL_OCCT` 知道有没有后端可注册。

---

## 4. 一次求值走下来

```
Document::createGeom / rebuild
  → IGeometryBuilder::build            （过渡 shim，P3 会换成显式传内核）
    → evaluate_feature_model(model, kernel, deflection)      中间层
      → evaluate_feature_bodies(model, kernel)               中间层
        → 逐个特征：
            RectProfile   → kernel.make_rect_face(w, h)
            Extrude       → kernel.extrude(profile, depth)
            Fillet        → resolve_edge(kernel, body, feature)  ← 索引 + 几何指纹
                            → kernel.fillet(body, {edge_id}, radius)
            …
      → kernel.tessellate(body, deflection)                  后端
```

指纹的**测量**在后端（`measure_edges`），**匹配算法**在中间层（`edge_fingerprint.cpp`：
阈值、代价、唯一性检查、报错）。换后端时拓扑命名照样工作，因为后端只负责“这条边长什么样”。

---

## 5. 现状与下一步

**已落地**：

- 接口层、OCCT 后端、求值器与指纹匹配搬进中间层、`register_linked_kernels()`；
- **第二个后端：Truck（Rust）**，见 §6；设置里可以选内核；
- 跨后端验收网 `tests/kernel_conformance_tests.cpp`：同一批场景对每个注册的内核跑一遍。

**两个后端现在各自会什么**（`KernelCapabilities::verbs` 是真相，UI / 测试都按它走）：

| 动词 | OCCT | Truck |
|---|---|---|
| 矩形 / 圆 / 多边形轮廓、拉伸、平移、边的枚举与测量、三角化、包围盒 | ✅ | ✅ |
| 布尔（并 / 交 / 差） | ✅ | ✅（经 `truck-shapeops`） |
| 圆柱 | ✅ | ❌ |
| 圆角 / 倒角 | ✅ | ❌ |
| STEP / IGES / BREP 读写 | ✅ | ❌ |

**过渡件（P3 收尾）**：

- `geometry_builder()` 仍是全局单例，调用方（`Entity::createGeom`、各 `rebuild`）还没显式接内核；
- `evaluate_feature_model(model, deflection)` 两参重载用进程默认内核，只为不打断老调用方；
- 导入路径仍是自己的 `Shape` / `IShapeOps`，没有并入 `Body`。

**还没做**：

- Truck 的相邻面法线：`measure_edges` 只填位置 / 方向 / 长度（指纹少了消歧的那一维）；
- `measure_edges` 一次算完所有边（含法线）。大零件上可以拆成“粗测量 + 按候选补法线”；
- 能力位接 UI 灰按钮（现在只有设置里的内核下拉）。

---

## 6. 第二个后端：Truck（Rust）

**为什么是它**：Truck 是 Rust 写的开源 B-rep 内核（MIT/Apache）。它的价值不在“比 OCCT 强”，
而在两点：① 能编到 WASM，将来浏览器里想要可编辑的模型，OCCT 进不去；② 用一个能力更弱的
内核反过来验证接口是不是真的中立。

**桥怎么分**（Rust 与 C++ 之间只有一层薄桥）：

```
C++  truck/truck_kernel.cpp     实现 ModelKernel 动词，Body 包一个 u64 句柄
        │  extern "C"（POD + 裸指针）
Rust    truck-bridge/src/lib.rs  持有 Truck 拓扑（handle table）、跑动词、序列化网格
        └─ truck-modeling / topology / meshalgo / shapeops
```

桥的纪律（写在 `lib.rs` 顶部）：

1. 每个导出函数 `catch_unwind`，panic 绝不穿 FFI，转错误码 + `truck_last_error()`；
2. 跨边界只有 `#[repr(C)]` 结构体和裸指针，不传 `String` / `Vec` / `Option`；
3. 网格缓冲由 Rust 分配、C++ 拷走后调 `truck_verts_free` / `truck_indices_free`；
4. 桥里没有业务逻辑——特征树、指纹、错误文案全在中间层。

**怎么打开**：

```powershell
cmake --preset msvc -DTAMIAS_ENABLE_TRUCK_KERNEL=ON
cmake --build --preset debug --target tamias
```

需要 Rust 工具链（`cargo`）；CMake 用 `add_custom_command` 调 `cargo build --release` 产静态库
（**必须声明 Rust 源码为依赖**，否则改了 `lib.rs` 也不会重编——在这里踩过一次）。
打开后设置 → Modeling → Kernel backend 就能选，重启生效。

**踩过的坑**：

- 两个后端量出来的边必须可比，所以 `EdgeMeasure` 统一到 Tamias Y-up、由中间层归一化，
  指纹因此加了 `edge_fp_version`；
- `EdgeMeasure.key`：OCCT 用 `std::hash<TopoDS_Shape>`，Truck 用 `Edge::id()`（曲线 Arc 指针）。
  没有它，中间层分不清“同一条棱被两个面各枚举一次”和“两条真的不同的棱”；
- 共面的两个体做布尔是退化情形：Truck 直接返回 none。验收用例里把工具体挪开一点避开它。

相关：[特征树求值器](FEATURE-TREE-EVALUATOR.md)、[几何边界](ISHAPE-OPS.md)、[渲染 RHI](RENDERING.md)。
