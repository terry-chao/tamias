# 浏览器造型后端改为服务端 OCCT —— 实施方案（B 路线）

> 状态：**方案，未落一行代码**。范围经用户确认：wasm **不链** OCCT，浏览器侧的造型交给已有的
> 几何服务（.NET 壳 + `tamias_server.dll`，原生 OCCT）。
>
> 关联：[服务端语言决策](docs/DECISION-SERVER-LANGUAGE.md)（§8.5 的「几何结果的传输格式」
> 由本文回答）、[建模内核](docs/MODELING-KERNEL.md)、[引擎 WASM + Web 查看器](docs/WEB.md)、
> [WebGL 默认化清单](WEBGL-EXP-IMPLEMENTATION-PLAN.md)。
>
> **先读 §1**：本方案的成本不在「加一个 HTTP 客户端」，而在把 `IGeometryBuilder` 这条**同步缝**
> 改成异步——因为它同时被**命令路径**和**逐帧 LOD 路径**使用。

---

## 0. 先钉住三个事实（实测，非推断）

| 问题 | 答案 | 证据 |
|---|---|---|
| 现在 wasm 的造型后端 | **Truck**（Rust → `wasm32-unknown-emscripten`） | `build/wasm/CMakeCache.txt`：`TAMIAS_ENABLE_OCCT:BOOL=OFF`、`TAMIAS_ENABLE_TRUCK_KERNEL:BOOL=ON`；[CMakeLists.txt:109](CMakeLists.txt#L109) 在 `EMSCRIPTEN` 下 FORCE 关 OCCT |
| 前端界面 | **React 18 + TypeScript + Vite** | [web/package.json](web/package.json)；[App.tsx](web/src/App.tsx) / [ViewCube.tsx](web/src/ViewCube.tsx) / [viewer.ts](web/src/viewer.ts)；构建挂进 CMake（[src/wasm/CMakeLists.txt:53-107](src/wasm/CMakeLists.txt#L53-L107)） |
| 服务端有没有 OCCT | **有**，而且与客户端同一份源码 | `server` 预设继承 `msvc`（`TAMIAS_ENABLE_OCCT=ON`）；[server_api.h](src/server/server_api.h) |

**结论**：B 路线不需要任何交叉编译，也不需要客户端链接 OCCT。要做的是**把造型求值挪过边界**，
并把那条缝变成异步。

---

## 1. 三条硬约束（决定了方案的形状，不是可选项）

### 1.1 build 缝是同步的

- [geom_builder.h:14-15](src/engine/modeling/evaluate/geom_builder.h#L14-L15)：
  `virtual Result<MeshCpu> build(const FeatureModel&, double deflection) const = 0;` —— 同步、`const`、一次返回网格。
- 命令路径是**同步直调**：
  `Session::dispatch` → `Result<void>`（[session.h:41](src/host/session.h#L41)）→
  `CommandSystem::dispatch` → `command->execute()`（[command_system.cpp:45](src/command/core/command_system.cpp#L45)，无线程、无队列）→
  `createGeom()`（柱 [:163](src/command/create/component/create_primitive_command.cpp#L163)、门 [:175](src/command/create/component/create_primitive_command.cpp#L175)、
  窗 [:187](src/command/create/component/create_primitive_command.cpp#L187)、梁 [create_beam_command.cpp:99](src/command/create/component/create_beam_command.cpp#L99)）→
  `Entity::createGeom()`（[entity.cpp:151-157](src/entity/core/entity.cpp#L151-L157)）→ `geometry_builder().build(...)`。
  所有基本体走的是**同一条**缝。
- **后果**：任何「在 `build()` 里发网络请求」的实现，都会把 `dispatch` 变成异步调用。
  这是本方案的主要改动面。

### 1.2 同一条缝还在**逐帧**路径上（最容易踩的雷）

- [document.cpp:423-439](src/engine/document/document.cpp#L423-L439) `make_tess_fn` 返回的 lambda 调
  `geometry_builder().build(model, deflection)`（[:428-430](src/engine/document/document.cpp#L428-L430)），
  经 `enqueue_lod_request`（[:459-464](src/engine/document/document.cpp#L459-L464)）进 `TessWorker`。
- wasm 下 `TessWorker` **没有线程**（[tess_worker.cpp:13-14](src/engine/modeling/evaluate/tess_worker.cpp#L13-L14)），
  由 `ViewerHost::render()` **每帧** `pump(1)` 并落地结果
  （[viewer_host.cpp:414](src/wasm/viewer_host.cpp#L414)、[:441-442](src/wasm/viewer_host.cpp#L441-L442)）。
- 另有一处同步调用：`ensure_feature_coarse_lod` 在 `add_entity` 内直接 `build(...)`
  （[document.cpp:393-394](src/engine/document/document.cpp#L393-L394)，被 [:617](src/engine/document/document.cpp#L617) 调用）。
- **后果**：
  1. 远程求值**绝不能**在帧内阻塞——帧内只允许 poll 完成队列（与 `TessWorker` 同款）；
  2. 在 `requestAnimationFrame` 回调里挂起 Asyncify 的 wasm 栈是雷区，必须避免；
  3. LOD 路径要么异步化，要么在远程模式下停用（见 §7.5）。

### 1.3 服务端 ABI 明文禁止几何过界

- [server_api.h:15-16](src/server/server_api.h#L15-L16)：「**边界粗粒度**：……几何（网格 / Brep /
  内核对象）**永远不过边界**，过边界的只有命令名、参数文本和只读字符串」。
- 现状读侧只有实体的 name / kind（[server_api.h:93-99](src/server/server_api.h#L93-L99)、
  [Contracts.cs:28](server/csharp/Tamias.Server/Contracts.cs#L28)）——**今天拿不到一个三角形**。
- [DECISION-SERVER-LANGUAGE.md §8.5](docs/DECISION-SERVER-LANGUAGE.md#L195) 把「几何结果的传输格式」列为**未决事项**；
  [§6](docs/DECISION-SERVER-LANGUAGE.md#L170) 记 `.trscn` 导出**只有读没有写**。
- **后果**：本方案必须**扩展 ABI（v1 → v2，只追加）**，并替那份文档把 §8.5 回答掉（答案见 §5）。

---

## 2. 好消息：两端的编解码器**已经存在**

这决定了 §5 的协议可以很小。

| 方向 | 现成的东西 | 位置 |
|---|---|---|
| 请求：特征树 | `write_feature_model` / `read_feature_model` | [document_io.cpp:242-278](src/engine/document/document_io.cpp#L242-L278) / [:280-323](src/engine/document/document_io.cpp#L280-L323) |
| 响应：三角网 | `write_mesh_cpu` / `read_mesh_cpu` | [mesh_binary.h:9-10](src/engine/io/mesh_binary.h#L9-L10) |
| 底层 | `BinaryWriter` / `BinaryReader`：显式小端、无 ABI 依赖（不 memcpy 结构体） | [binary_archive.h:14-205](src/engine/io/binary_archive.h#L14-L205) |

三个性质正好是协议需要的：

1. **确定性**：`write_feature_model` 对参数键排序（[:261-267](src/engine/document/document_io.cpp#L261-L267)），
   同一份特征树字节稳定 → 可以直接做缓存键；
2. **只存语义**：特征 id 由插入顺序隐含（[:241](src/engine/document/document_io.cpp#L241)），
   不含指针 / 句柄 / 内核类型；
3. **两边都已链接**：wasm 已链 `tamias::io`（[src/wasm/CMakeLists.txt:13](src/wasm/CMakeLists.txt#L13)），
   服务端是同一份源码。

⇒ §8.5 的答案：**复用既有二进制编解码器 + 一个版本化信封**。不发明新格式，不走 base64。

---

## 3. 方案选型

| | S1（推荐）异步 build 缝 + 无状态 evaluate | S2 = S1 + 本地预测档 | S3 命令级委派（服务端持有文档） |
|---|---|---|---|
| 边界位置 | `IGeometryBuilder::build` → 远程 OCCT | 同 S1，另有本地 Truck 先出预测网格 | 浏览器只发命令，服务端 Session 求值 |
| 谁是权威内核 | 服务端 OCCT | 服务端 OCCT（Truck 仅装饰） | 服务端 OCCT |
| 文档归属 | **客户端**（撤销/选择/拾取都不动） | 客户端 | **两份**（客户端镜像 + 服务端），需要同步 |
| 每实体请求数 | 1（含 LOD 多档，见 §7.5） | 1 | 1~N（每条命令）+ 快照拉取 |
| 新增传输格式 | 有现成 codec（§2） | 同 S1 | 需要 `.trscn` **写**侧（今天没有）+ 场景替换协议 |
| 撤销语义 | 不变 | 不变 | 变成「作业级」，与桌面不一致 |
| 主要风险 | 同步缝改异步 | 双内核三角网不一致（视觉 pop） | 两份文档的一致性；多用户会话存储（§8.1） |
| 复用现有 HTTP 端点 | 新增 1 个 | 新增 1 个 | `POST /v1/sessions/{id}/batch` 等已存在 |

**推荐 S1**，理由：

1. 它**字面**达成「wasm 的造型后端是 OCCT」——权威求值由服务端 OCCT 完成；
2. **只有一处真相**：特征树、撤销栈、选择、拾取全留在客户端，不产生第二份文档，
   也就绕开了 §8.1（会话在进程内存、无认证、无黏性路由）；
3. 中间层（`evaluator` / `edge_fingerprint`）**一行不改**——服务端跑的是同一份
   `evaluate_feature_model`（[evaluator.cpp:187-207](src/engine/modeling/evaluate/evaluator.cpp#L187-L207)），
   圆角的「索引 + 指纹」解析仍在形状所在的那一侧执行，不需要把拓扑命名搬过边界；
4. S3 需要的 `.trscn` 写侧今天不存在，是**另一轮工作量**，且会把撤销语义改掉。

S2 作为可选的体验档（先显示 Truck 预测网格，OCCT 结果到达后替换），但它引入
「两套三角网」的一致性面，**建议放在 P4 之后单独评估**，不进主路径。

---

## 4. 推荐架构（S1）

```
浏览器（wasm）                                        几何服务（.NET 壳 + tamias_server.dll）
┌───────────────────────────────────────────┐        ┌────────────────────────────────────────┐
│ React UI (web/)                           │        │ ASP.NET Core 壳                        │
│   dispatch() / newDocument() / 状态角标     │        │   POST /v1/geometry/evaluate           │
│        │ embind                            │        │   GET  /health（已有，做能力握手）        │
│        ▼                                   │        │        │ P/Invoke（ABI v2，只追加）      │
│ ViewerHost::dispatch  ──┐                  │        │        ▼                               │
│   （命令路径，异步化）    │                  │        │ tamias_geometry_evaluate               │
│ ViewerHost::render 每帧  │                  │        │        │                               │
│   poll 完成队列 ◄────────┘                  │        │        ▼                               │
│        │                                   │        │ evaluate_feature_model(model, OCCT)     │
│        ▼                                   │        │   ← 与桌面/服务端同一份中间层           │
│ RemoteGeometryBuilder                      │        │        │                               │
│   ── 特征树字节（FeatureModel codec）───────┼───────►│        ▼                               │
│   ◄─ 三角网字节（MeshCpu codec）────────────┼────────│ write_mesh_cpu / read_mesh_cpu         │
│ Document：intern_mesh + tess_cache_ 落地    │        └────────────────────────────────────────┘
└───────────────────────────────────────────┘
```

**关键点**：边界两侧**都不出现内核类型**——请求是特征树字节，响应是三角网字节。
这既满足 [server_api.h:15-16](src/server/server_api.h#L15-L16) 的粗粒度纪律，
也让「换内核」对客户端完全透明。

---

## 5. 线上协议（回答 §8.5）

### 5.1 端点

| 方法 | 路径 | 说明 |
|---|---|---|
| `POST` | `/v1/geometry/evaluate` | 无状态：收特征树字节，回三角网字节。**不建会话** |
| `GET` | `/health` | 已有（[Program.cs:63](server/csharp/Tamias.Server/Program.cs#L63)）；客户端用它做能力握手 |

### 5.2 请求信封（`application/octet-stream`）

```
magic   "TGEV"      4B
version u16         信封版本，从 1 起
flags   u32         bit0 要 EdgeMeasure；bit1 要 faces 范围；bit2 要多档 LOD（见 §7.5）
deflection f64      目标弦高
count   u32         本批特征树条数
per model：
  tag   u64         调用方自定义标签（客户端用它对应实体，原样回显）
  blob  u64 + bytes 复用 write_feature_model 的输出
```

### 5.3 响应信封

```
magic   "TGEM"      4B
version u16
status  u32         TAMIAS_SERVER_OK / E_*
count   u32
per result：
  tag     u64       原样回显
  ok      u8
  meshes  u32       档数（1，或 2~3）
  per mesh：
    lod      u8     0=Box 1=Coarse 2=Work 3=Close（保留）
    bounds   6×f64
    blob     u64 + bytes   复用 write_mesh_cpu 的输出
  可选：edges    u32 + N×(key u64, mid 3×f32, dir 3×f32, has_dir u8, length f64,
                            n1 3×f32, n2 3×f32, has_n1 u8, has_n2 u8)
```

`EdgeMeasure` 的字段（[kernel.h:97-113](src/engine/modeling/kernel/kernel.h#L97-L113)）全是 POD，
可以照 `mesh_binary` 的写法加一个 `write_edge_measures`，与 `MeshCpu` 同一个文件风格。

### 5.4 错误

- **传输/参数层**：HTTP 4xx/5xx + JSON 错误体，直接复用 [Contracts.cs:46](server/csharp/Tamias.Server/Contracts.cs#L46) 的 `ErrorResponse`；
- **单个模型求值失败**：HTTP 200，响应信封里该条的 `ok=0` + 一条错误字符串
  （一批里有一条失败不该让整批失败——8 条 `new_document` 命令里坏一条仍应出其余 7 个）。

### 5.5 版本与兼容

信封 `version` 与 ABI 版本**各管各的**：

- ABI 版本走既有的启动握手（[Program.cs:27-35](server/csharp/Tamias.Server/Program.cs#L27-L35)）：
  原生库与壳必须同版本，这是既有纪律；
- 信封版本管**客户端 ↔ 服务端**：客户端遇到不认识的版本 → 报明确错误并**降级回本地 Truck**，
  不要猜。

### 5.6 为什么是二进制而不是 JSON

1. 编解码器已存在（§2），写 JSON 反而要新写一套 mesh 编解码 + base64（体积 ×1.33）；
2. 一个中等零件的三角网是 MB 量级，JSON 的解析开销和体积都不划算；
3. 现成的 `BinaryWriter` 是**显式小端、逐字段**，没有结构体 memcpy 的对齐 / ABI 陷阱，
   跨编译器（MSVC ↔ clang/wasm）安全。

> 唯一代价：`curl` 调试不直观。缓解：P0 里加一个 `?debug=json` 只回 **摘要**
> （顶点数 / 三角数 / bounds），把「肉眼可查」和「传输高效」分开。

---

## 6. 服务端改动

### 6.1 C++ ABI：v1 → v2（只追加）

在 [server_api.h](src/server/server_api.h) 末尾追加（**不改既有签名、不改枚举既有值**，
把 `TAMIAS_SERVER_ABI_VERSION` 抬到 2）：

```c
// 无状态几何求值：收请求信封字节，写回响应信封字节。
// out_reply 由原生侧分配，调用方用 tamias_geometry_free 释放。
// 失败返回 E_*，原因见 last_error()；成功但个别模型失败走信封内 per-item 状态。
TAMIAS_SERVER_API std::int32_t tamias_geometry_evaluate(
    const std::uint8_t* request, std::int32_t size,
    std::uint8_t** out_reply, std::int32_t* out_size);
TAMIAS_SERVER_API void tamias_geometry_free(std::uint8_t* reply);
```

**为什么用「分配 + 释放」而不是既有的 `fill_utf8` 截断语义**：那个约定是为短字符串设计的
（[server_api.h:89-90](src/server/server_api.h#L89-L90)），网格是 MB 量级、**截断等于数据损坏**。
新约定必须区分「缓冲区不够」和「数据就这么多」。

### 6.2 实现

- 新增 `src/server/geometry_api.cpp`（与 `server_api.cpp` 并列，不进既有文件以免打乱它）；
- 入口按 §4.1 的纪律 **`try/catch` 全包**（[DECISION-SERVER-LANGUAGE.md §4.1](docs/DECISION-SERVER-LANGUAGE.md#L132)）
  ——异常穿过 `extern "C"` 进 .NET 是进程崩溃；
- 求值直接调**已存在的** `evaluate_feature_model(model, kernel, deflection)`
  （[evaluator.h:21-23](src/engine/modeling/evaluate/evaluator.h#L21-L23)），内核取
  `default_kernel()`（服务端注册的是 OCCT）；
- 注意 `default_kernel()` 是 `std::call_once` 的单例（[linked_kernels.cpp:63-77](src/engine/modeling/linked_kernels.cpp#L63-L77)）：
  **不是线程安全的**（[server_api.h:17-18](src/server/server_api.h#L17-L18) 同款约定）。
  .NET 侧对几何端点也要按请求串行化，或把一个 `ModelKernel&` 加锁使用。

### 6.3 .NET 壳

1. `Program.cs` 加路由 `POST /v1/geometry/evaluate`（原始字节进、原始字节出）；
2. 新增 `NativeGeometry.cs` 做 P/Invoke（照 `NativeServer.cs` 的写法）；
3. **必须加 CORS**：现在整个 `server/` 里 `AddCors` / `UseCors` **零命中**，
   而预览在 `:3000`、几何服务在 `127.0.0.1:5199`（[serve-geometry.cmd](scripts/serve-geometry.cmd)）
   ——跨源，不加 CORS 浏览器直接拦掉。两条路：
   - **(a) 加 CORS**：白名单可配置，默认只放开发用的 `http://localhost:3000` / `127.0.0.1:3000`；
   - **(b) 同源**：让几何服务顺带把 `build/wasm/bin` 当静态目录（`UseStaticFiles`），
     页面从 `:5199` 打开。**更干净**，且顺带解决生产部署的同源问题。
   建议 (b) 为主、(a) 作为需要分离部署时的配置项。

### 6.4 无状态的意义（顺带绕开 §8.1）

`evaluate` **不建会话、不碰 SessionRegistry**，所以它不受
[DECISION-SERVER-LANGUAGE.md §8.1](docs/DECISION-SERVER-LANGUAGE.md#L187)（会话在进程内存、
多副本要黏性路由）影响。这是 S1 相对 S3 的一个实打实的好处：几何这条链路可以水平扩。

> 但要说清代价：**特征树字节仍然离开了浏览器**。对「模型不许出内网」的 BIM 场景，
> 这是必须提前告知的（[§3.2](docs/DECISION-SERVER-LANGUAGE.md#L108) 已经列过这条代价）。

---

## 7. 客户端（wasm）改动

### 7.1 HTTP 客户端：wasm 现在**零** HTTP 能力

全仓 `EM_ASYNC_JS` / `emscripten_fetch` / `emscripten_sleep` **零命中**，`web/src` 里
`fetch(` 也零命中。这一块是**从零写**：

- 新增 `src/wasm/geometry_client.{h,cpp}`：`EM_ASYNC_JS` 包 `await fetch(...)`，
  返回二进制；
- 前提已满足：viewer 已带 `-sASYNCIFY=1`（[src/wasm/CMakeLists.txt:29](src/wasm/CMakeLists.txt#L29)），
  React 侧已有 `settle()` 处理「embind 返回 Promise」的情形（[viewer.ts:107-113](web/src/viewer.ts#L107-L113)）；
- 超时 / 断线必须显式处理：`AbortController` + 明确的错误码，**不能**让 wasm 栈永久挂起。

### 7.2 异步化 build 缝

[geom_builder.h:12-16](src/engine/modeling/evaluate/geom_builder.h#L12-L16) 的
`IGeometryBuilder` 改成两阶段（**保留原同步接口给桌面**）：

```cpp
class IGeometryBuilder {                    // 桌面：不变
  virtual Result<MeshCpu> build(const FeatureModel&, double) const = 0;
};
class IAsyncGeometryBuilder {               // wasm：新增
  // 提交；返回不透明 job id。0 = 拒绝（例如离线）。
  virtual std::uint64_t submit(const FeatureModel&, double deflection) const = 0;
  // 非阻塞取回：没完成返回 nullopt；完成返回结果（取走即失效）。
  virtual std::optional<Result<MeshCpu>> take(std::uint64_t job) const = 0;
  virtual bool available() const = 0;       // 几何服务健康检查
};
```

### 7.3 `Document` 的 pending 状态与落地

**复用已有的延迟落地机制**，不要新造：`TessWorker` + `apply_completed_tess_jobs`
（[document.cpp:467-482](src/engine/document/document.cpp#L467-L482)）已经就是
「稍后算完 → `intern_mesh` → `tess_cache_.bind` → 返回待上传 id」。

需要新增：

1. `Document::add_entity` 支持**无网格实体**（现在签名是 `add_entity(unique_ptr<Entity>, MeshCpu)`，
   按值收网格，[document.cpp:564](src/engine/document/document.cpp#L564)）→ 加一个重载 /
   允许空网格 + 标记 `pending`；
2. 无网格实体必须能被 `render_items`（[document.cpp:690-753](src/engine/document/document.cpp#L690-L753)）、
   `bounds()`（[document.h:394-410](src/engine/document/document.h#L394-L410)）安全跳过，
   否则「先建实体、网格后到」会崩或把相机框到原点；
3. 结果到达后：`intern_mesh` → 绑到实体 → `recompute_scene()` + `mark_dirty()`
   （[document.cpp:618-619](src/engine/document/document.cpp#L618-L619) 同款）→ 交给
   `ViewerHost::upload_document()`（[viewer_host.cpp:205-227](src/wasm/viewer_host.cpp#L205-L227)）。

### 7.4 `ViewerHost`：提交在命令路径，取回在帧内

| 位置 | 现在 | 改成 |
|---|---|---|
| `dispatch`（[viewer_host.cpp:466-477](src/wasm/viewer_host.cpp#L466-L477)） | 同步求值 → `recompute_scene()` → `upload_document()` | 发远程请求（不阻塞）→ 立刻 `upload_document()`（显示已有/占位几何） |
| `render`（[viewer_host.cpp:414-442](src/wasm/viewer_host.cpp#L414-L442)） | `TessWorker::pump(1)` + `apply_completed_tess_jobs()` | 旁边加 `poll_remote_geometry()`，**只读完成队列，绝不发请求** |

这条分工是 §1.2 的直接后果：**网络只能从命令路径发起**，帧内只做无阻塞的取回。

### 7.5 LOD 与请求数

- 档位枚举是 `Box / Coarse / Work / Close`（[mesh_lod.h:14-19](src/engine/render/resource/mesh_lod.h#L14-L19)），
  但**会被求值的只有 `Coarse` 和 `Work`**：`Box` 由渲染侧共享单位立方体兜底、从不走求值
  （[document.cpp:442](src/engine/document/document.cpp#L442)、[tess_cache.h:16](src/engine/document/tess_cache.h#L16)、
  [scene_graph.cpp:203-206](src/engine/render/scene/scene_graph.cpp#L203-L206)），`Close` 保留给选中 / 剖切；
  弦高见 [mesh_lod.h:27-29](src/engine/render/resource/mesh_lod.h#L27-L29)（Coarse 0.6 / Work 0.05 / Close 0.02）；
- 于是今天每个实体最多被求值 **2 次**（`ensure_feature_coarse_lod` [document.cpp:380-403](src/engine/document/document.cpp#L380-L403)
  一次 Coarse + 命令路径一次 Work），另有 `enqueue_lod_request`（[document.cpp:441-465](src/engine/document/document.cpp#L441-L465)）按像素误差补档；
- 远程模式下**一次请求把 2~3 档一起要回来**（信封 `flags` bit2，§5.2）：服务端反正已经把模型算出来了，
  多离散两档比多一轮往返便宜；
- `RemoteGeometryBuilder::build(model, deflection)` 按 `deflection` 命中对应档；
  未命中就先回 `Work` 并在后台补 `Coarse` / `Close`。这样 LOD 路径**不需要**改成异步。

### 7.6 `new_document` 的 8 条命令

[viewer_host.cpp:100-155](src/wasm/viewer_host.cpp#L100-L155) 在启动时同步跑 4 根柱 + 4 根梁。
远程模式下这是 8 次求值：用 §5.2 的**批量信封一次发**（8 条），拿到后再逐个落地。
若批量仍然慢，则显示「计算中」状态并让相机先框到柱网包围盒。

### 7.7 前端（React）改动

| 文件 | 改动 |
|---|---|
| [viewer.ts](web/src/viewer.ts) | 新增 embind 导出：`geometryServiceState()`（`offline`/`connecting`/`ready`）、`pendingGeometryCount()`；沿用 `settle()` 处理 Promise 返回 |
| [App.tsx](web/src/App.tsx) | 顶栏加服务状态角标（复用 `backendName()` / `backendExperimental()` 那段 `brand` 的写法与 `.wip` 样式）；状态栏显示 pending 数；离线时把建模命令按钮灰掉 |
| [App.tsx](web/src/App.tsx) | 文件选择器 `accept`（[:431](web/src/App.tsx#L431)）暂不动——STEP/IGES 入口属于后续一轮，不混进来 |

「离线灰按钮」的判据用现成的能力位：`KernelCapabilities`（[kernel.h:65-79](src/engine/modeling/kernel/kernel.h#L65-L79)）。
[MODELING-KERNEL.md:168](docs/MODELING-KERNEL.md#L168) 已把「能力位接 UI 灰按钮」列为待办，本方案正好是它的第一个真实用例。

---

## 8. 分阶段落地

| 阶段 | 内容 | 验收（可实测） | 状态 |
|---|---|---|---|
| **P0** 通传输 | ABI v2 `geometry_evaluate`（§6.1）+ .NET 端点 + 同源 / CORS；wasm 加 `geometry_client`；加一个临时 embind 导出 `evaluateRemote(name, bytes)` 做冒烟 | 浏览器 console 拿到一个手写特征树的**顶点数**；`curl` 打服务端返回正确的三角数 | ❌ |
| **P1** 异步缝 | `IGeometryBuilder` 两阶段化（§7.2）；`Document` 的 pending + 无网格实体（§7.3）；`ViewerHost::render` 加取回 | 单条远程求值能在**不阻塞 rAF** 的前提下落地；帧时间不出现尖峰 | ❌ |
| **P2** 接命令路径 | `RemoteGeometryBuilder` 成为 wasm 默认；`new_document` 批量化（§7.6）；Truck 降为离线回退 | 「新建文档」出的柱网**由服务端 OCCT 求值**；关掉服务端 → 自动回落 Truck 并出角标 | ❌ |
| **P3** LOD 路径 | 一次请求把 Coarse + Work（含 Close）一起要回来（§7.5）；`ensure_feature_coarse_lod` 走远程 | 远近切换不触发网络阻塞；每个实体的请求数从「每档一次」降到 **1** | ❌ |
| **P4** 界面与文档 | 状态角标 / pending 读数 / 离线灰按钮（§7.7）；同步 [WEB.md §4「OCCT」段](docs/WEB.md#L130)、[MODELING-KERNEL.md §6](docs/MODELING-KERNEL.md#L172)、[DECISION-SERVER-LANGUAGE.md §8.5](docs/DECISION-SERVER-LANGUAGE.md#L195) | 三处文档不再与实现矛盾 | ❌ |

**P0 之前的一个前置决定**：Truck 在 wasm 里是**保留**（离线回退 + P0/P1 期的对照组）还是**移除**。
建议保留到 P2，P4 再按实测体积决定——注意 B 路线**不给 wasm 加 OCCT**，
所以移除 Truck 会让 `tamias_viewer.wasm`（现在 2.79 MB）**变小**，而不是变大。

---

## 9. 验收标准（汇总）

1. 服务端日志出现 `POST /v1/geometry/evaluate`，且请求体里的特征树来自浏览器；
2. 同一个 `.tdoc` 在桌面（OCCT）与浏览器（远程 OCCT）得到**一致的三角数**（跨后端一致性测试
   `tests/kernel_conformance_tests.cpp` 的口径可以复用）；
3. **离线降级**：关闭 `TamiasGeometryServe` 后，页面在 N 秒内显示离线角标、建模命令灰掉，
   **已加载的文档仍可查看/旋转/拾取**（不能白屏）；
4. **不阻塞渲染**：远程求值期间帧时间不出现超过一帧的尖峰（帧内只 poll，§7.4）；
5. 桌面路径**零回归**：`DefaultGeometryBuilder` 未变，桌面仍是本机 OCCT/Truck。

---

## 10. 风险与回退

| 风险 | 影响 | 缓解 / 回退 |
|---|---|---|
| Asyncify 在帧内挂起 | 页面卡死（最难查的一类） | 设计上禁止：网络只在命令路径发起（§7.4）；帧内只 poll |
| 请求数爆炸（每实体 × 每档） | 交互卡顿 | 批量信封 + 一次回多档（§5.2 / §7.5） |
| 服务端未启动 / 断线 | 建模不可用 | `available()` 健康检查 → 灰按钮 + 回退本地 Truck；文档查看不受影响 |
| 无网格实体的空窗期 | 崩溃或相机框到原点 | §7.3 的第 2 条：`render_items` / `bounds` 必须跳过 pending 实体 |
| 数据主权：特征树出浏览器 | 合规风险 | 部署文档写明；这是 B 路线的固有代价（[§3.2](docs/DECISION-SERVER-LANGUAGE.md#L108)） |
| ABI v1→v2 | 旧原生库与新壳对不上，**直接拒绝启动**（既有握手） | 这是刻意设计（[Program.cs:27-35](server/csharp/Tamias.Server/Program.cs#L27-L35)）；升级必须两边同编 |
| 服务端 `default_kernel()` 单例并发 | 数据竞争 | 几何端点串行化或加锁（§6.2） |

**完全回退**：wasm 预设里把 `RemoteGeometryBuilder` 换回 `DefaultGeometryBuilder`
（即恢复今天的行为），服务端端点留着不影响任何东西。这条回退是**一个编译期开关**，
不涉及数据迁移。

---

## 11. 需要拍板的未决问题

1. **同源还是 CORS**（[§6.3](#63-net-壳)）：建议同源（几何服务顺带伺服静态页），
   但会影响 F5 链路（现在预览在 `:3000`）；
2. **Truck 的去留**（§8 前置决定）：P2 后是保留为离线回退，还是 P4 移除换体积；
3. **`.tdoc` 里的已烘焙网格还要不要**：B 路线下，打开 `.tdoc` 只吃 MESH/SCEN 缓存
   （[WEB.md:130-132](docs/WEB.md#L130-L132)）的行为是否改变——建议**不改**，
   远程求值只在**编辑/新建**时发生；
4. **S2（本地预测档）做不做**：能显著改善手感，代价是双三角网的一致性面（§3）。
