# SMLib 引入方案

> **结论：不换内核，加第三个后端。** SMLib 落在 [`kernel.h`](src/engine/modeling/kernel/kernel.h) 后面，
> 默认关闭，桌面 / 服务端可用，WASM 不支持。先用 3 天验证 6 个硬假设（§7 P0），再决定要不要投 4 周。
>
> 本文的「SMLib」指 NVIDIA 2026-10 开源的 **USD BRep v1.0.0** 里的 `SMLib` + `SM_API` 两层，
> 不是整个包（USD / Hydra / Python 都不进 Tamias）。
> 事实基线日期 2026-10-22；版本、体积、依赖都是本机对发布包实测的，不是文档抄的。
>
> **2026-10-22 进展**：P0 + P1 已落地——`KernelBackend::Smlib`、C++17 C 门面、13 个动词、
> 设置项；`KernelConformance` 11 条在 OCCT / SMLib 上全绿（SMLib 侧见 §13 实测）。
> 实现与坑点已写进 [docs/MODELING-KERNEL.md](docs/MODELING-KERNEL.md) §7，以那份为准。

---

## 0. 一页纸

| 问题 | 答案 |
|---|---|
| 换掉 OCCT？ | **不。** 默认仍是 OCCT，SMLib 是可选后端（`TAMIAS_ENABLE_SMLIB_KERNEL=OFF`）。 |
| 主要收益 | Apache-2.0；倒角 / 变半径 / 抽壳 / 偏置 / 缝合这一组比 OCCT 的 API 更直接；给「换内核」这条架构主张补第二个真实实现。 |
| 主要代价 | 破坏性 API（每个动词都要 copy）、进程级串行、无 WASM、无 STEP/IGES、Debug 配置不可用、上游 1.0.0 太新。 |
| 工作量 | P0 3 天；做到 conformance 全绿 8–10 天；壳 + 分发 3–4 天；合计 **18–23 人日**（不含 USD 桥）。 |
| 最大风险 | 你以为引入的是「更强的内核」，实际引入的是「一套必须自己兜住的并发 / 拷贝 / 坐标系契约」。兜不住就会静默改坏别的特征分支。 |
| 第一步 | 跑 §7 的 P0。任何一条硬指标不过，就停在文档，不合并代码。 |

---

## 1. 先确认：动机是什么

「引入 SMLib」不是一个可验收的目标。四个常见动机，代价差一个数量级：

| 动机 | SMLib 是答案吗 | 说明 |
|---|---|---|
| **许可证**：想去掉 LGPL | 部分是 | OCCT 是 LGPL-2.1 + 链接例外。SMLib 是 Apache-2.0，确实更省心；但只要还保留 STEP/IGES 导入，OCCT 就还在链接表里（§2）。除非放弃 CAD 交换格式，否则这是一次「减依赖」而不是「去 LGPL」。 |
| **质量**：某些倒角 / 抽壳 / 布尔做不出来 | 可能 | SMLib 的强项正是 fillet / blend / shell / heal。但「换内核」不会自动修好你手上那 5 个失败件——必须拿真实失败样例做 A/B。没有样例库，这条动机不可验收。 |
| **性能**：大模型布尔太慢 | 未知 | 没有任何公开的同场景对比。P0 量出来再说；而且 SMLib 的破坏性 API 会额外引入拷贝成本（§5.1）。 |
| **USD / Omniverse / AI agent** | 是，但这是另一个项目 | `BrepArray`、Hydra、Python 绑定都在这个包里。Tamias 现在没有 USD 层，接进来是**新产品面**（USD 舞台、材质、引用），不是「加个后端」。不要把它塞进 kernel 后端，否则 OpenUSD 会进内核依赖树。 |

**建议的立项口径**：以「第三后端 + 可对比」立项，把动机写成可验收的指标（例如「在 `assets/samples/` 的 N 个失败件上，SMLib 的 fillet 成功率 ≥ OCCT」）。四个动机里一个可验收的都没有，就先别立项。

**还要先问一句**：你要的能力，是不是只是 `kernel.h` 没暴露？
抽壳、偏置、放样、扫掠 OCCT 全都有（`BRepOffsetAPI_MakeThickSolid`、`BRepOffsetAPI_MakeOffset`、`BRepOffsetAPI_ThruSections`、`BRepPrimAPI_MakeSweep`），只是当前 13 个动词里没有。
**给 OCCT 加一个 `shell` 动词 ≈ 2–3 天；加一个内核后端 ≈ 4 周。** 如果动机是「要抽壳」，先加动词。

---

## 2. SMLib 是什么（实测事实）

来源：[NVIDIA-Omniverse/usd-brep](https://github.com/NVIDIA-Omniverse/usd-brep)、[v1.0.0 release](https://github.com/NVIDIA-Omniverse/usd-brep/releases/tag/v1.0.0)（2026-10-09，Apache-2.0）。

- 血统：Solid Modeling Solutions 的商用 NURBS/BRep 内核（NLib 血统），NVIDIA 2022 收购，2026-10 开源。不是实验品。
- 能力：布尔、圆角（含变半径 / G1–G3 / per-edge）、斜角、抽壳、偏置、缝合 / 修复、面边查询、质量属性、离散、扫掠 / 放样 / 管道。
- 发布物：Windows x64、Linux x64、Linux aarch64（macOS 只能自己编源码；**没有 wasm**）。
- Windows 包里实测：
  - `SMLib.dll` 12.1 MB、`SM_API.dll` 2.0 MB；**只依赖 VC runtime**（`dumpbin /dependents` 确认，没有 OpenUSD、没有 OpenGL、没有 Python）。
  - 头文件 238 个（`include/SMLib`）+ 15 个（`include/SM_API`）。
  - 另有 `SM_API_USD`、`BREP_STAGING`、`OCCT_BREP_IMPORT/EXPORT`、`HYDRA_USD_BREP`——全部围绕 USD，**不需要**。
- `SM_API` 是 C 风格的稳定包装（`SmApiStatus` 是 `long`，句柄是裸指针），底层 `SMLib` 是完整内核。**后端只应该依赖这两层。**
- 数据交换：`SM_API` 只有原生 `.smb` 读写；**没有 STEP / IGES**。包里那两个 `OCCT_BREP_*` 是「OCCT `.brep` 文件 ↔ USD BrepArray」的文件格式桥（自带解析器，不链 OCCT），不是 OCCT 与 SMLib 的内存互转——所以**不存在**「OCCT 造型 + SMLib 倒角」的混合路线（除非自己写形状转换器，不建议）。
- 并发：官方头文件写得很直白——`SM_API` 用进程级全局 context，**不是线程安全的**，建议「一个 worker 进程跑一个并发 job」，shipped build 单线程。这一条会直接改 Tamias 的求值并发模型（§5.2）。

---

## 3. 这次做什么，不做什么

**做**

- `KernelBackend::Smlib`，编进来才可选；不编进来时零影响。
- 覆盖现有全部 13 个动词（`RectFace` / `CircleFace` / `PolygonFace` / `Extrude` / `Boolean` / `Transform` / `Cylinder` / `Edges` / `MeasureEdges` / `Tessellate` / `Bounds` / `Fillet` / `Chamfer`），通过 `tests/kernel_conformance_tests.cpp`。
- 设置里可选、重启生效；与 OCCT 可来回切。
- 安装包只多两个 DLL。

**不做**

- 不做默认内核（P3 再拍）。
- 不支持 wasm（不是「暂时不支持」，是这条产品线就不该有它，见 §6）。
- 不替换 STEP / IGES 导入；不引入 OpenUSD。
- 不改 `.tdoc`、特征树、指纹算法、命令系统。
- 不为 SMLib 扩新特征（抽壳 / 放样）。先证明「同一份 `.tdoc` 能跑」，再谈扩能力。

**为什么值得做**：`kernel.h` 的整个卖点是「后端可换」。现在只有 OCCT（强、但 LGPL + 单一实现）和 Truck（弱、但恰好证明接口中立）。SMLib 是第一个「和 OCCT 同量级、许可更宽松」的实现——它能把这条主张从「设计上可换」变成「工程上换过」。这件事本身有架构价值，和它比 OCCT 强不强是两回事。

---

## 4. 接缝：只动 6 个地方

现有架构已经把缝留好了。除了注册、UI、构建，中间层**一行不用改**。

| # | 改动 | 文件 | 说明 |
|---|---|---|---|
| 1 | 枚举 + 字符串 | `kernel.h:29`、`kernel_registry.cpp:10` | 加 `Smlib` / `"smlib"`；顺手加 `from_string`，别让字符串比较散在 `main.cpp` 和设置对话框里（现在就是这样）。 |
| 2 | 注册 | `linked_kernels.cpp` 的 `register_linked_kernels()` | `#if TAMIAS_HAS_KERNEL_SMLIB` → `register_smlib_kernel_backend()`。 |
| 3 | 新后端目录 | `src/engine/modeling/smlib/` | 见 §5 的文件清单与 CMake。 |
| 4 | 构建开关 | `CMakeLists.txt:102` 附近、`modeling/CMakeLists.txt:40` 附近 | `TAMIAS_ENABLE_SMLIB_KERNEL`，照 Truck 那两段抄。 |
| 5 | 设置 | `app/main.cpp:73`、`settings_dialog.cpp:267` | 现在是一个 `truck ? Truck : Occt` 的三元表达式 + 一个 `truck ? ... : ...` 的标签分支。第三个后端进来必须换成查表，否则一定写错。 |
| 6 | 打包 | `cmake/TamiasPackage.cmake` | 它已经 glob 运行目录下的 `*.dll`，WiX 又是 `!(bindpath.Payload)\**` 全量收——所以**不用改 `.wxs`**，只要把两个 DLL 拷进 payload（`POST_BUILD` copy），再把 Apache-2.0 许可文本放进去。 |

后端目录建议：

```
src/engine/modeling/smlib/
  smlib_kernel.h/.cpp     ModelKernel 动词
  smlib_body.h/.cpp       RAII：SmBrep / 轮廓曲线 + 所有权
  smlib_mesh.h/.cpp       SmPolyBrep → MeshCpu（含 Z-up→Y-up、法线）
  smlib_error.h           SmApiStatus → Result
  CMakeLists.txt
```

---

## 5. 六个技术差异（方案的核心）

这六条才是「引入 SMLib」真正的工程量。接口那 13 个动词是简单的，难的是它们的语义和 Tamias 的假设不一致。

### 5.1 破坏性 API vs 不可变 `BodyRef`

`SM_API` 的造型动词全是 **in-place**：布尔成功时**修改第一个操作数、删除第二个**；失败时两个都可能被改过但还活着。官方示例甚至专门写了注释提醒不要重复释放（`Examples/SM_API/boolean_bounds.cpp`）。

Tamias 的契约正相反：`BodyRef = shared_ptr<const Body>`，一个体可能被多个下游特征引用（DAG，不是链）。后端如果直接就地改，会把兄弟分支一起改坏——而且**不会报错**，只会在别的构件上出现莫名的几何。

**对策（P1 必做）**：

```cpp
// boolean(a, b)：第一版两个都深拷贝（正确性优先）；b 的所有权以后可以优化成转移
auto lhs = copy_brep(a);              // SmApiBrepCopy：深拷贝，O(BRep)
auto rhs = copy_brep(b);
SmBrep* result = nullptr;
const SmApiStatus st = SmApiBooleanDifference(lhs, rhs, result);  // 成功：result == lhs，rhs 已删
```

- 第一版**一律拷贝**，正确性优先。
- 拷贝成本是真实的：`SmApiBrepCopy` 是深拷贝，大构件上可能几十毫秒。P0 必须量（§7）。
- P3 再优化成 copy-on-write：把 `boolean`/`fillet`/`transform` 的入参从 `const Body&` 改成 `const BodyRef&`，后端看 `use_count() == 1` 就地消费。这会改接口签名、动 OCCT/Truck/求值器，**没有 P0 的数据不要做**。
- 同时给 conformance 加一条**跨后端**不变量测试（现在没有）：

```cpp
TEST(KernelConformance, BooleanDoesNotMutateInputs) {
  // 记录 a/b 的 bounds 与三角数，boolean 后必须一模一样
}
```

这条测试先写、先让现有两个后端绿；接上 SMLib 后，只要漏了拷贝它立刻红。它保护的不只是 SMLib——任何后端把「不可变」当儿戏都会被抓到。

### 5.2 线程：`SM_API` 不是线程安全的

官方注释（`SmApiGeneral.h`）说得很清楚：全局默认 context、遍历计数器没加锁、**不要在一个进程里并发调 `SmApi*`**，shipped build 单线程；`SmApiCreateContext()` 要在起线程之前调一次。

Tamias 现在有三条线程可能碰内核：UI 线程（建特征）、`TessWorker` 后台线程（离散）、服务端请求线程。OCCT 后端能扛，SMLib 不能。

**对策**：

- 后端内部一把**进程级** `std::mutex`，所有 `SM_API` 调用（含 `SmApiTessellate`）都在锁下；不是每个对象一把，是全局一把。
- `SmApiCreateContext()` 放在 `register_smlib_kernel_backend()` 里 `call_once`，早于任何文档求值。
- 把「本后端串行」写进能力/文档。服务端的吞吐要按串行重算；如果将来要并行，只能**多进程**，不是多线程。
- 这也会拖慢离散：`TessWorker` 的队列在 SMLib 后端下和 UI 求值互斥。可接受，但要让 Profiling 面板能看出来（SMLib 名字已经在 `kernelName()` 里）。

顺带一提：`default_kernel()` 是 `call_once` 单例，本身就不是线程安全的切换点（已知问题，见 `GEOMETRY-SERVICE-PLAN.md` §6.2）。这把全局锁救不了它，别混为一谈。

### 5.3 坐标系与体模型：Z-up，轮廓不是 Brep

证据：`SmApiCreateBox` 的 length/width/height 沿 X/Y/Z；圆柱沿 +Z 从底面起；角度从 XY 平面的 X 轴起。

沿用 OCCT 后端已经在用的映射（`occt_kernel.cpp:56-75`）：

```
Tamias (x, y, z)  →  SMLib (x, -z, y)
SMLib (x, y, z)   →  Tamias (x, z, -y)
```

三条纪律：

1. **后端内部一律存 SMLib 原生 Z-up**，只在输入处转进去、输出处转出来（mesh / bounds / EdgeMeasure）。这和 OCCT 后端的做法一致。
2. 轮廓面在 SMLib 的 XY 平面（z=0）建，拉伸沿 +Z——对应 Tamias 的 XZ 平面、+Y。
3. 注意**二维镜像会翻转多边形绕向**：`(x, 0, z) → (x, -z, 0)` 是 2D 反射。`make_polygon_face` 要么反序传点，要么显式确认 SMLib 的 sweep 不看绕向；P1 用一个凹多边形测。

> 小坑：`occt_kernel.cpp:57` 的注释写「绕 X 转 -90°」，但代码做的是 `(x, -z, y)`（+90°）。**以代码为准**，别照着注释写 SMLib 的转换，否则符号会反。

**体模型的一处分叉**：OCCT 的 `Body` 永远是 `TopoDS_Shape`（轮廓也是 face）；SMLib 的轮廓是曲线环，没有 Brep。于是 `SmlibBody` 得是 `Profile(曲线) | Solid(SmBrep)` 两种形态：`extrude` 接 Profile，`boolean` / `fillet` / `chamfer` / `tessellate` / `bounds` 只接 Solid。接错必须返回 `Err`，不要静默给空网格——这是最容易在 P1 埋雷的地方。

### 5.4 边与拓扑命名

`edges()` 的枚举顺序就是 `EdgeId`；`measure_edges()` 的下标必须与之对齐；`fillet` 收到的 `EdgeId` 是前者产出的。`EdgeMeasure.key` 只要求「同一个 body 生命周期内」有效，跨求值的持久化是指纹层的事。

SMLib 的对应物：

- `SmApiGetEdges(brep, edges)` 拿边表，`EdgeId` = 下标。
- `SmApiEdgeComputeLength` / `SmApiEdgeTangent` / `SmApiEdgeBoundingBox` 拿长度、切向、范围。
- **相邻面法线 `SM_API` 没给**，要降到 SMLib 层：`SmEdge::GetFaces()` / `GetEdgeuses()`（`SmEdge.h`），再用面法线在参数中点求值。这是后端唯一需要 include SMLib 内部头的地方——可接受（OCCT 后端也一样），但要接受它「不是稳定 API」的事实。
- 闭环边沿用 OCCT 后端的约定：24 个采样点的质心当 `mid`、`has_dir = false`（`occt_kernel.cpp:182-209`）。

**一个必须先定的契约**：`SmApiGetEdges` 返回的是**唯一几何边**还是**按面重复**？现在 conformance 里有一条 `EXPECT_GT(with_shared_key, 0u)`（`kernel_conformance_tests.cpp:232`），它假设「同一条棱被两个面枚举两次、key 相同」——那是 OCCT `TopExp_Explorer` 的实现细节，**不是接口契约**。如果 SMLib 只返回唯一边，这条会红。

处理方式（推荐）：把契约写成「允许重复；重复项必须共享 key；也允许只列唯一几何边」，把断言放宽成「有重复时必须同 key」，并在 `kernel.h` 的注释里写死。**不要**为了迁就一条测试去人为制造重复。

> 实测：SMLib 的盒子就是 12 条唯一边（`EdgeId` = 下标，`key` = 下标 + 1）。conformance 的
> 断言已按「唯一枚举也合法」放宽，见 `tests/kernel_conformance_tests.cpp`。

`EdgeMeasure.key` 用 `SmEdge*` 指针（或边表下标）都行，前提是同一个 body 内稳定——和 Truck 用 `Edge::id()`（指针）是同一档。

### 5.5 离散：参数映射 + 朝向

`SmApiTessellate(brep, polybrep, SmTessellationParams)`。参数是弦高 + 曲线角 + 曲面角 + 最大边长 + 长宽比；默认**弦高 0（忽略）、角度 25°**。

映射：

```cpp
SmTessellationParams params;
params.dChordHeightTol     = deflection;   // Tamias 传的是绝对弦高，直接对上
params.dCurveAngleTolDeg   = 20.0;         // 与 OCCT 默认视觉对齐，P0 校准
params.dSurfaceAngleTolDeg = 20.0;
```

三个坑：

1. **弦高默认是 0**。不显式设，SMLib 只按角度细分，同一个 `deflection` 出来的三角形数和 OCCT 不可比。P0 要拿同一个盒子 / 圆柱对比三角形数与视觉。
2. **离散可能出朝向错的三角形**（NVIDIA 自己在文档里承认）。Tamias 两个渲染后端都是 `cullMode = None`（`vulkan_device.cpp:1857`、`webgpu_device.cpp:1112`），不会破洞，但光照会错。对策：优先用 `SmPolyBrep` 的 per-face 法线做顶点法线；朝向按面统一；P1 加一条「闭合体体积 > 0 且法线朝外」的检查。
3. 默认「任一失败面就整体失败」。大装配上经常是个别面炸，建议 `bAllowPartial = TRUE` + `SmTessellationReport`，把失败面数写进日志，而不是整块网格变空。

网格提取：`SmPolyBrep` 的 region → shell → face → loop 遍历（`SmPoly.h`），多边形做扇形三角化，拷进 `MeshCpu`，逐顶点做 Y-up 转换。

### 5.6 错误、内存、异常

- `SmApiStatus` 是 `long` 错误码，**没有 message**。建一张 `SM_ERR_*` → 文案表，`Result` 里给用户看得懂的话，别把 `-17` 直接怼到 UI。
- 对象是裸指针，所有权规则**逐函数不同**（布尔删 B、扫掠不消费曲线、tessellate 输出归调用方、copy 返回新对象）。统一包一层 RAII：

```cpp
class SmlibBrep {
 public:
  SmlibBrep() = default;
  ~SmlibBrep() { delete brep_; }              // 对象由 SM_API 的堆分配：Debug/Release 混用会炸（§6）
  SmlibBrep(const SmlibBrep&) = delete;
  SmlibBrep& operator=(const SmlibBrep&) = delete;
  SmBrep* get() const { return brep_; }
 private:
  SmBrep* brep_ = nullptr;
};
```

- 后端入口统一 `try/catch (...)`：SM_API 是 C 风格，但内部是 C++，异常不能穿到 Tamias。
- context 只有 `Create` / `GetOrCreate`，**没有 Destroy**。退出时会有常量级泄漏，内存检查要加白名单，否则 CI 永远红。

---

## 6. 构建与分发

### 6.1 三个选项

| 方案 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| **A. 预编译包** | 解压 v1.0.0 的 7z 到 `3rdparty/smlib/`，CMake imported target | 半小时能跑通，适合 P0 | Release / v142 / `/MD`；Tamias Debug 是 `/MDd`，**堆不匹配**（见下） |
| **B. 外部 prefix（推荐）** | `scripts/smlib.ps1` 准备 prefix，`TAMIAS_SMLIB_PREFIX` 指过去 | 和 `TAMIAS_OCCT_PREFIX` / `scripts/occt-wasm.ps1` 同一套路；可钉版本、可打 patch、CI 可缓存 | 多一个脚本 + 一次前置构建 |
| **C. 源码 vendored 进 CMake** | 把 SMLib 源码搬进 `3rdparty/` 自己写 CMake | 一次搞定 | 上游是 premake + Packman（OpenUSD 3.12 / Python 3.12 / VS2019 v142），重写一套 CMake 是长期负担；**不推荐** |

P0 用 A，P1 起切 B。**不要试图把 SMLib 加进 `vcpkg.json`**——没有 port，而且它的构建系统不是 CMake。

### 6.2 CMake 形状

```cmake
# CMakeLists.txt
option(TAMIAS_ENABLE_SMLIB_KERNEL "Build the SMLib (USD BRep) modeling kernel backend." OFF)

# cmake/TamiasDependencies.cmake
set(TAMIAS_SMLIB_PREFIX "" CACHE PATH "SMLib / SM_API install prefix (include/ + lib/).")
if(TAMIAS_ENABLE_SMLIB_KERNEL AND NOT TAMIAS_SMLIB_PREFIX)
  message(FATAL_ERROR "TAMIAS_ENABLE_SMLIB_KERNEL=ON 需要 -DTAMIAS_SMLIB_PREFIX=<prefix>（见 scripts/smlib.ps1）")
endif()
```

后端 target：

```cmake
add_library(tamias_kernel_smlib STATIC smlib_kernel.cpp smlib_mesh.cpp smlib_body.cpp)
add_library(tamias::kernel_smlib ALIAS tamias_kernel_smlib)
target_include_directories(tamias_kernel_smlib SYSTEM PUBLIC
  "${TAMIAS_SMLIB_PREFIX}/include/SMLib"
  "${TAMIAS_SMLIB_PREFIX}/include/SM_API")
target_link_libraries(tamias_kernel_smlib PUBLIC tamias::kernel tamias::base tamias::graphics)
target_link_libraries(tamias_kernel_smlib PRIVATE
  "${TAMIAS_SMLIB_PREFIX}/lib/SMLib.lib"    # Linux: libSMLib.so（全路径）
  "${TAMIAS_SMLIB_PREFIX}/lib/SM_API.lib")
```

几个必须做的细节：

- **`SYSTEM` include**：238 个老头文件，`/W4 /permissive-` 下一定刷警告。不要为了它们把整个项目的警告级别降下来。
- **拆两层 target（实测必须）**：SMLib 头只能在 **C++17** 下编（C++20 起 `SmTreeVertex::operator==` 的模板实例化歧义），而 `kernel.h` 是 C++23。落地形状是 `tamias_smlib_c`（C++17，只 include SMLib，C 门面）+ `tamias_kernel_smlib`（C++23）。
- **必须定义 `UNICODE` / `_UNICODE`**：发布包是 Unicode 构建（`TCHAR = wchar_t`）；不定义的话 `SmObject::Dump` / `smos_*` 的修饰名对不上，链接会报 11 个未解析符号。
- **运行期只需要两个 DLL**（`dumpbin` 证实只依赖 VC runtime）。安装包里**不要**带 `extraLibs/`、`usdpy/`、`plugin/`，那是 USD 那一套。
- **Debug 配置禁用**：Tamias Debug 用 `/MDd`，SMLib 发布包是 `/MD`。跨 CRT 传裸指针再 `delete` 是未定义行为。两种解法：① 只在 `Release` / `RelWithDebInfo` 打开（推荐，P1 这么做）；② 用 `repo.bat` 编一份 Debug 的 SMLib（P3 再考虑）。
- **运行库拷贝**：`TamiasPackage.cmake` 会 `file(GLOB .../*.dll)` 把运行目录的 DLL 全部 install，`tamias.wxs` 又是 `!(bindpath.Payload)\**` 全量收——所以只加一条 `POST_BUILD` 拷贝到 `$<TARGET_FILE_DIR:tamias>`，安装包自动带上，**不用改 WiX**。Apache-2.0 的 `LICENSE` / `THIRD_PARTY_NOTICES` 放进 payload 即可。
- **Linux**：发布包有 x86_64 / aarch64，但 Tamias 的 Linux 预设走 vcpkg + GCC 14。SMLib 的 `.so` 是 GCC 编的，ABI 大概率能过，但 `GLIBCXX_USE_CXX11_ABI=1` 和 libstdc++ 版本要 P0 验；验不过就只在 Windows 开，别硬撑。

---

## 7. 分阶段与决策门

### P0 — 验证性 spike（3 天，可回退，不合并产品代码）

目标：用**最小程序**回答 6 个硬问题，全部有数字。

1. 能不能在 `/std:c++17` + `/MD` 下编译并链接（不改 Tamias 任何代码）？
2. `SmApiBrepCopy` 在 1 万 / 10 万面体上的耗时（决定 §5.1 的拷贝策略）。
3. 布尔（plate − 4×4 圆柱阵列）+ 圆角 + 抽壳的耗时，对比 OCCT 同场景。
4. `SmApiTessellate` 在 `deflection = 0.05` 下的三角形数 / 耗时，与 OCCT 的差异；法线朝向是否一致。
5. 坐标：box 的 bounds / 边长 / 闭环边 `mid` 是否与 OCCT 对齐（±1e-4）。
6. `SmApiGetEdges` 是唯一边还是按面重复（决定 §5.4 的契约）。

产出：`spike/smlib_probe.cpp` + 一页数字表（填进附录 C）。**退出标准：6 条全过，且 copy < 50 ms/体、离散质量肉眼可接受。** 任一条不过，就把数字写进附录 C，不进入 P1。

### P1 — 后端 + conformance（8–10 天）

顺序很重要，**测试先行**：

1. 先加两条跨后端不变量：`BooleanDoesNotMutateInputs`、`TransformDoesNotMutateInputs`。此时 SMLib 还没接，OCCT/Truck 应该绿。
2. `smlib_body` + `smlib_error` + 全局锁 + context 初始化。
3. 基础动词：`make_rect_face`（`SmApiCreateRectangle` 出 4 条曲线）→ `extrude`（`SmApiCreateLinearSweep`，`bCapEnds = TRUE`）→ `tessellate`。先让第一个 conformance 用例绿。
4. `circle` / `polygon` / `cylinder` / `transform` / `boolean`（带拷贝）。
5. `edges` / `measure_edges`（含 SMLib 层取相邻面法线）。
6. `fillet` / `chamfer`（`SmApiFilletEdges`，`lXSectType = 1` 圆角、`0` 线性 = 斜角）；`caps.multi_edge_fillet = true`。
7. `bounds`、`capabilities`、失败路径（空布尔、退化圆角必须返回 `Err`，不能崩）。

目标：`ctest -R KernelConformance` 三后端全绿；`TAMIAS_ENABLE_SMLIB_KERNEL=ON` 且 `TAMIAS_ENABLE_OCCT=ON` 同时成立。

### P2 — 壳与分发（3–4 天）

- `kernel_backend_from_string()` 收口字符串解析；`main.cpp` / 设置对话框改成查表。
- 设置里第三项（`SMLib (NVIDIA, experimental)`），提示里写清「桌面/服务端；浏览器不支持」。
- wasm 侧不用改：`available_kernels()` 只列编进来的后端，SMLib 不编，自然不出现。
- 安装包两个 DLL + 许可文本；`BUILD.md` 加前置说明。
- 服务端：确认串行化（SMLib 后端下不允许并发请求进内核）。
- 文档：`docs/MODELING-KERNEL.md` 加一节；`docs/WEB.md` 明确 wasm 不编它。

### P3 — 质量与默认（4–6 天，可选）

- copy-on-write（只有 P0 的数据说拷贝是瓶颈才做）。
- 失败样例库 A/B（需要你提供真实失败件）。
- partial tessellation + 法线修复。
- 决定是否默认。**我的建议是长期保持 OCCT 默认**：SMLib 无 STEP/IGES、无 wasm、串行、Debug 不可用，这四条在可预见的将来都不适合当默认。

### P4 — 另立项

USD 桥（BrepArray / Hydra / `OCCT_BREP_IMPORT`）。这是产品面，不是内核后端；不要为了「顺便」把 OpenUSD 拖进来。

---

## 8. 验收标准（可执行）

| 类别 | 标准 |
|---|---|
| 正确性 | `KernelConformance` 全绿（若某个动词最终没做，必须**声明不支持**并干净报错，不能假装成功）。 |
| 不可变 | `BooleanDoesNotMutateInputs` / `TransformDoesNotMutateInputs` 三后端全绿。 |
| 几何一致 | 同一 `.tdoc` 在 OCCT / SMLib 下：bounds 差 < 1e-3（相对包围盒对角线），封闭体体积差 < 1e-3 相对，三角形数差 ≤ 30%（同 `deflection`）。 |
| 性能 | 1000 个 box 的批量求值不慢于 OCCT 的 1.5×；单次 boolean 的拷贝开销有记录、不超过 P0 定的预算。 |
| 稳定性 | 100 次随机参数重建不崩；失败用例全部返回 `Err`；无跨 DLL 释放。 |
| 并发 | 两线程同时求值不崩（全局锁生效）；服务端串行化有日志证据。 |
| 分发 | 关闭开关时产物**零差异**（DLL 不进包、不链、不注册）；打开时安装包只多两个 DLL + 许可。 |
| 文档 | `MODELING-KERNEL.md`、`BUILD.md`、`WEB.md` 三处写清楚边界。 |

---

## 9. 风险与退出

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| 上游 1.0.0 太新，bug / 行为变化 | 高 | 中 | 锁版本、只 opt-in、不默认；问题样例进 `tests/` |
| 破坏性 API 的拷贝成本 | 中 | 高 | P0 量化；COW；实在不行就只在小构件上可用 |
| 非线程安全 | 高 | 高 | 全局锁 + 服务端串行 + 文档；不要幻想「读操作能并发」 |
| Debug/Release CRT 混用 | 中 | 高 | Debug 禁用；或编 Debug 版 |
| 无 STEP/IGES | 确定 | 中 | OCCT 继续负责导入；产品上讲清楚 |
| 无 wasm | 确定 | 低 | 浏览器线继续 OCCT/Truck |
| 许可 / 专利（NURBS 老代码） | 低-中 | 高 | 法务过 Apache-2.0 + `THIRD_PARTY_NOTICES`；保留出处 |
| 双内核长期维护 | 中 | 中 | conformance 网兜住；不扩动词就不加负担；默认只有一个 |
| 上游「不接受贡献」 | 中 | 中 | 补丁自己维护，别指望上游合并 |

**退出条件**（任一触发就停，不合并后端）：

- P0 的 copy > 50 ms/体，且 COW 也救不回来；
- 离散质量在标准件上不可接受，且无法用法线/朝向修复；
- 只能在 Release / RelWithDebInfo 下跑（Debug 用不了），而团队日常就是 Debug；
- 需要改 `kernel.h` 的语义才能接（说明接口不中立，先改接口，别硬接）。

---

## 10. 工作量

| 阶段 | 人日 | 产出 |
|---|---|---|
| P0 spike | 3 | 数字表 + go/no-go |
| P1 后端 + conformance | 8–10 | 13 个动词、三后端全绿 |
| P2 壳 + 分发 | 3–4 | 设置、安装包、文档 |
| P3 质量 / 默认 | 4–6 | COW、失败样例、结论 |
| **合计** | **18–23** | 单人 4–5 周；两人并行 2–3 周（P1 的动词可以拆） |

不含 USD 桥。不含「给 SMLib 扩新特征」（那是另一个数量级）。

---

## 11. 备选路线（不引后端也能做的事）

按性价比排序，先看前两个：

1. **给 OCCT 加动词**（2–3 天/个）：`shell`、`offset`、`loft`。如果动机是「能力不够」，这条比引内核便宜一个数量级。
2. **只修 conformance 的洞**：补「不可变」「失败路径」「线程」三类不变量测试。这些测试对现有两个后端也有价值，而且和引不引 SMLib 无关。
3. **SMLib 当离线工具**（5–7 天）：独立 CLI 做修复 / 格式转换 / 批量倒角，不进程内链接。风险最低，拿不到交互式能力。
4. **混合内核**（不推荐）：OCCT 造型 + SMLib 倒角。包里**没有**内存级形状互转（`OCCT_BREP_*` 走 USD 文件），要自己写转换器；两个内核同进程的线程/内存模型也不兼容。成本高于收益。

---

## 12. 需要你拍板的三件事

1. **动机优先级**：许可 / 质量 / 性能 / USD，哪个是第一？四个都要，就是四个项目。
2. **边界**：接受「桌面 + 服务端、非 wasm、非默认、Debug 不可用」吗？不接受的话，方案要重写（得先解决 Debug 构建和 wasm 交叉编译，后者基本无解）。
3. **对比样例**：有没有 3–5 个真实的失败件（`.tdoc` / `.step` / 截图）？没有样例，P1 的「质量」验收只能靠 conformance 的标准件，说服力有限。

---

## 附录 A：动词映射

| `ModelKernel` | `SM_API` | 备注 |
|---|---|---|
| `make_rect_face(w,h)` | `SmApiCreateRectangle(center, w, h, curves)` | 返回**轮廓**（4 条曲线），不是 Brep；`SmlibBody` 要能装两种形态 |
| `make_circle_face(r)` | `SmApiCreateCircle(center, r, SmCircle*&)` | 同上 |
| `make_polygon_face(loop)` | `SmApiCreateLineSegment` × n 闭合 | 注意 2D 镜像导致绕向翻转 |
| `extrude(profile, d)` | `SmApiCreateLinearSweep(curves, +Z, d, TRUE)` | 曲线不被消费；`SmBSplineCurve*` 要收进 `SmTArray<SmCurve*>` |
| `boolean(a,b,op)` | `SmApiBrepCopy(a)` → `SmApiBooleanDifference/Union/Intersection` | **成功删 b**；a 就地改 |
| `transform(body,t)` | `SmApiBrepCopy` → `SmApiTranslate` | 平移向量要转 Z-up |
| `cylinder(r,h,c,axis)` | `SmApiCreateCylinder(base, r, h)` + `SmApiTransform` | 先沿 +Z 建，再转到 `axis`；`base = c - axis*h/2` |
| `edges(body)` | `SmApiGetEdges` | `EdgeId` = 下标；唯一/重复待 P0 确认 |
| `measure_edges(body)` | `SmApiEdgeComputeLength` / `SmApiEdgeTangent` + `SmEdge::GetFaces` + 面法线 | 相邻面法线必须降到 SMLib 层 |
| `fillet(body, ids, r)` | `SmApiFilletEdges(..., 1 /*circular*/, 1 /*G1*/, 1.0)` | 就地改，先拷贝 |
| `chamfer(body, ids, d)` | `SmApiFilletEdges(..., 0 /*linear*/, ...)` | 线性截面就是斜角 |
| `tessellate(body, defl)` | `SmApiTessellate` → `SmPolyBrep` 遍历 | 弦高映射、partial、法线朝向 |
| `bounds(body)` | `SmApiBrepBoundingBox(brep, FALSE, min, max)` | 转 Y-up 后**重算** AABB（旋转后不再轴对齐） |

`KernelCapabilities`：

```cpp
caps.verbs = RectFace | CircleFace | PolygonFace | Extrude | Boolean | Transform |
             Cylinder | Edges | MeasureEdges | Tessellate | Bounds | Fillet | Chamfer;
caps.multi_edge_fillet = true;
caps.variable_radius_fillet = false;   // 内核有，但 kernel.h 没有这个参数，别虚报
caps.step_import = caps.step_export = false;
caps.iges_import = caps.iges_export = false;
caps.brep_import = caps.brep_export = false;   // 只有原生 .smb
```

## 附录 B：P0 检查清单

```powershell
# 1. 解包（Windows tar 能读 7z）
tar -xf usd_brep@1.0.0.*.windows-x86_64.release.7z -C C:\dev\usd-brep

# 2. 确认运行期只依赖 VC runtime
dumpbin /dependents C:\dev\usd-brep\lib\SM_API.dll
dumpbin /dependents C:\dev\usd-brep\lib\SMLib.dll

# 3. 最小 spike：box → 4x4 圆柱 cut → fillet → tessellate
#    /std:c++17 /EHsc /MD，include/SM_API + include/SMLib，链 SM_API.lib + SMLib.lib
#    记录：copy / boolean / fillet / tessellate 的 ms，三角形数，bounds，法线朝向

# 4. 确认边的枚举方式
#    盒子：SmApiGetEdges 返回 12 还是 24？同一条棱的指针是否相同？
```

## 附录 C：P0 实测结果

P0 交付的就是这张表；表里的数字决定 P1 的拷贝策略和 §5.4 的契约怎么写。

| 指标 | OCCT | SMLib | 结论 |
|---|---|---|---|
| 编译：`/std:c++17` + `/MD` 直接可用？ | — | ✅（但必须 `UNICODE`） | 头文件只能 C++17；拆 C 门面 |
| `SmApiBrepCopy` 2×2×2 盒子 ×100 | — | 2.97 ms（≈0.03 ms/盒） | 小体拷贝可以忽略 |
| `SmApiBrepCopy` 10 万面 | — | 未测（P3 再量） | 大件仍需实测 |
| 布尔（2×2×2 盒 − R0.3 圆柱） | — | 1.46 ms | 可用 |
| 圆角（单边 R0.2） | — | 2.14 ms，6 → 7 面 | 可用；chamfer 同路径 |
| 离散（`deflection=0.05`，切孔盒） | — | 2.26 ms / 80 三角形 | 质量待视觉对比 |
| 盒子 bounds 对齐（±1e-4） | — | ✅ | 坐标映射正确 |
| 闭环边 `mid` 对齐 | — | ✅（`EvaluatePoint`） | 用参数中点，未做质心 |
| 法线朝向一致 | — | ✅（按面法线校 winding） | flat shading |
| `SmApiGetEdges`：唯一 / 按面重复 | — | **唯一**（盒子 12） | 契约放宽为「允许唯一」 |
| Linux `.so` 链接 | — | 未验 | 只在 Windows 开 |

> `SM_SUCCESS` 是 `1000L`（不是 0）；错误码是别的值。判断一律用 `status == SM_SUCCESS`。
