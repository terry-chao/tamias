# 测试

Tamias 的自动化测试几乎全是 **GoogleTest 可执行文件 `tamias_tests`**：不启动 Qt、不创建窗口、不碰真实 GPU。`ctest` 发现并跑这些用例。除此之外没有第二套测试体系。缺的那些怎么补，见 [§7 缺口怎么测](#7-缺口怎么测)。

> 约 **144** 条 `TEST()`（2026-09 盘点）。数字会变，以 `tamias_tests --gtest_list_tests` 为准。

---

## 1. 怎么跑

桌面预设默认 `TAMIAS_BUILD_TESTS=ON`。WASM 预设强制 `OFF`（见 [`CMakeLists.txt`](https://github.com/terry-chao/tamias/blob/main/CMakeLists.txt)）。

### Windows

终端里的 `cl` 没有标准库路径，必须先 `vcvars64.bat`，再编再测。仓库脚本会包一层（默认 Debug，编完跑 `RenderSceneGolden*`）：

```
.\scripts\build-tests.cmd
.\scripts\build-tests.cmd -BuildOnly
.\scripts\build-tests.cmd -Preset relwithdebinfo
```

或手写：

```
cmd /c "call `"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`" && cmake --build --preset debug --target tamias_tests"
ctest --test-dir build -C Debug --output-on-failure
```

或直接跑二进制、按套件过滤：

```
.\build\bin\Debug\tamias_tests.exe
.\build\bin\Debug\tamias_tests.exe --gtest_filter=RenderScene*
.\build\bin\Debug\tamias_tests.exe --gtest_filter=CommandArgText*:PluginHost*
```

RelWithDebInfo 可用 CMake test preset：`ctest --preset msvc-relwithdebinfo`。

### Linux

```
cmake --build --preset linux-relwithdebinfo --target tamias_tests
ctest --preset linux-relwithdebinfo
```

开关：`TAMIAS_BUILD_TESTS`（默认 ON）、`TAMIAS_USE_FETCHCONTENT`（找不到系统 gtest 时拉 [v1.15.2](https://github.com/google/googletest) zip）。`vcpkg.json` 也声明了 `gtest`。构建总述见 [BUILD.md](https://github.com/terry-chao/tamias/blob/main/BUILD.md)。

CMake 用 `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)`，并给 OCCT DLL 加 `PATH` 前缀，避免发现阶段找不到运行库。

---

## 2. 这是什么性质的测试

一个目标、一个框架：

| | 现状 |
|---|---|
| 框架 | GoogleTest（`GTest::gtest_main`） |
| 入口 | [`tests/CMakeLists.txt`](https://github.com/terry-chao/tamias/blob/main/tests/CMakeLists.txt) → `tamias_tests` |
| 链接 | `tamias::io` / `document` / `command` / `host` / `plugin` / `modeling` / `render` / `ifc` |
| **不**链接 | Qt 壳（`src/app`）、真实 RHI 设备、WASM / `web/`、C# 测试工程 |

按严格定义，仓库里**有单元测试，但大多数用例是无窗口的组件/集成测试**：

- **单元**：纯函数、小类型。例如网格吸附、AABB / 视锥、二进制归档 roundtrip、命令参数文本、插件表单解析、相机 clip 随距离缩放、IBL 贴图尺寸。
- **组件 / 集成（headless）**：`Document` + 命令系统 + OCCT 求值 + 场景图 Mock 录制 + `.tdoc` / `.trscn` IO。断言的是「文档状态 / draw 次数 / digest」，不是像素。
- **可选集成**：`PluginHost.LoadsManagedHelloCommands` 会启动 CLR、加载 `Tamias.Hello`。CLR 或插件 DLL 不在时 **`GTEST_SKIP`**，不会红。

没有 UI 自动化、没有 GPU 像素对比、没有 Web / C# 单测、没有 GitHub Actions 跑 `ctest`（CI 只部署文档站点）。

渲染侧停在「CPU 场景 + Mock RHI」，下一层（离屏 PNG）还没做，见 [渲染场景快照](RENDER-SCENE.md) 第 4 节。

---

## 3. 源文件与覆盖面

| 文件 | 套件 | 条数 | 测什么 |
|---|---|---|---|
| [`tests/smoke_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/smoke_tests.cpp) | `Math` `MeshIo` `BinaryArchive` `DocumentIo` `Document` `DocumentHistory` `Picking` `Camera` `ViewportFloor` `Modeling` `RenderConfig` `Occt` `FeatureModel` `Entity` `SketchEntity` `CurveGeom` `CommandSystem` `EntityGrip` `Bim` `Io` | 67 | 杂烩：数学、OBJ / `.tdoc`、拾取、实体建网格、命令撤销、特征 fillet/chamfer/union、BIM 宿主 |
| [`tests/scene_graph_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/scene_graph_tests.cpp) | `SceneGraph` `SceneDirty` `SceneGraphIncremental` `Document` | 19 | 展平 draw list、材质/透明通道、脏标记、增量更新；**Mock `CommandList`** 记 draw |
| [`tests/render_scene_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/render_scene_tests.cpp) | `RenderScene` `RenderSceneIo` | 17 | `.trscn` 烘焙、digest、roundtrip、视锥裁剪、水合后再录制 |
| [`tests/plugin_host_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/plugin_host_tests.cpp) | `CommandArgText` `PluginHost` `PluginManager` `PluginPointInputSession` `PluginPromptSpec` | 11 | 参数解析、HostApi 派发、点选会话；多数**不启动 CLR** |
| [`tests/host/session_test.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/host/session_test.cpp) | `SessionTest` | 5 | `Session` 的 dispatch / 撤销 / 选择 / 换文档 / 事件监听 |
| [`tests/location_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/location_tests.cpp) | `Location` | 3 | 墙的线定位、板相对标高、楼层 roundtrip |
| [`tests/ifc_spatial_tree_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/ifc_spatial_tree_tests.cpp) | `IfcSpatialTree` | 2 | [`assets/samples/spatial-tree.ifc`](https://github.com/terry-chao/tamias/blob/main/assets/samples/spatial-tree.ifc) 空间树；缺文件报错 |
| [`tests/ibl_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/ibl_tests.cpp) | `IblBake` | 1 | studio IBL 分辨率、有限非零辐射度 |
| [`tests/command_dispatch_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/command_dispatch_tests.cpp) | `CommandDispatch` | 8 | 梁/柱/圆柱/门/弧/矩形/楼层/`set_location`/`set_material`/`chamfer`、布尔交差 |
| [`tests/feature_boolean_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/feature_boolean_tests.cpp) | `FeatureModel` | 2 | 特征树布尔 Cut / Common |
| [`tests/camera_controller_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/camera_controller_tests.cpp) | `CameraController` | 4 | orbit / pan / frame_aabb / dolly_to_focus |
| [`tests/import_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/import_tests.cpp) | `MeshIo` `OcctImport` | 5 | 最小 GLB、拒绝 ASCII glTF、STEP/IGES/BREP 读写 |

夹具：IFC 样例、OBJ 样例（`cube.obj` / `alvin.obj`）。**仓库里没有 `.trscn` 金样**——[RENDER-SCENE.md](RENDER-SCENE.md) 写了「桌面导出 → 锁 digest」的做法，测试还没接上。

---

## 4. 按层：有测 / 薄 / 空

对照 [路线图分层](ROADMAP.md)。

| 层 | 有测 | 薄或缺口 |
|---|---|---|
| **数学 / 拾取** | 网格吸附、AABB、视锥 p-vertex、射线打盒子/变换节点/草图线、正交投影、`CameraController` 轨道/平移/框选 | — |
| **IO** | OBJ 读写、内存 OBJ、`BinaryArchive`、`.tdoc` 实体/NURBS/夹点 roundtrip、最小 GLB、STEP/IGES/BREP 读回 | ASCII glTF 本身未支持（已断言拒绝） |
| **Document / 场景图** | `render_items`、选择、历史快照、视锥剔除、分类色、增量脏标记 | 大模型 / 局部加载（LevelDB `.tdoc`）未实现也就没测 |
| **实体 / 命令** | 墙/盒子/线/板/圆/贝塞尔/B 样条/多段线/NURBS/梁/柱/圆柱/门/弧/矩形/楼层/`set_location`/`set_material`/`chamfer` 的 dispatch + 撤销；布尔 Fuse/Common/Cut | — |
| **造型 / OCCT** | tessellate box、改参数重求值、fillet 增面、chamfer 出网格、布尔并/交/差 | 求值器失败路径、特征依赖环、内核错误信息几乎没有断言 |
| **BIM** | 宿主对齐、墙改通知窗、关系 roundtrip、IFC **空间树**、`create_storey` / `set_location` | IfcGeom **几何导入未接线**，无几何 IFC 测试；轴网、多楼层工作流很薄 |
| **渲染 CPU** | 场景图录制、`.trscn` IO、Pin 扫描金样（digest / hydrate / Mock draw） | 像素级 PNG 金样未做 |
| **RHI / GPU** | 仅 `RenderConfig.OpenGlDoesNotShare`（线程共享策略） | **Vulkan / OpenGL / WebGL 设备、shader 编译、离屏像素全部未测** |
| **宿主 Session** | dispatch / undo / 选择 / reset / `HostEvent` 监听、`CameraController` | 与壳的手势对齐（Qt 按钮映射）未测 |
| **插件 C++** | HostApi、Ribbon 排序、点选会话、表单 spec | 对话框真正弹出、多视口、失败日志未测 |
| **插件 C#** | 加载 Hello 的命令 id（可 skip） | **没有** xUnit / NUnit；`Tamias.Api` / `Tamias.Host` / 示例插件无托管单测 |
| **Qt 壳 `src/app`** | `ViewportFloor` 标高聚类（无 Qt） | MainWindow、Ribbon、属性面板、主页、设置、i18n、最近文件、视口立方体：**零测试** |
| **WASM / `web/`** | 无。Emscripten 关闭 `TAMIAS_BUILD_TESTS` | `ViewerHost`、embind、React 壳无 vitest / Playwright；打开 `.trscn` 靠人眼 |
| **打包** | 无 | MSI / `cmake --install` 无自动化 |

---

## 5. 不完备清单（按优先级）

这些是「现在缺、以后补」的明确缺口，不是路线图功能本身。每条**具体怎么写、放哪、断言什么**见 [§7](#7-缺口怎么测)。

### 工程与门禁

1. **CI 不跑测试。** [`.github/workflows/pages.yml`](https://github.com/terry-chao/tamias/blob/main/.github/workflows/pages.yml) 只 `mkdocs build`。push / PR 不会编 `tamias_tests`，回归全靠本机 `ctest`。
2. **没有覆盖率。** 无 gcov / llvm-cov / OpenCppCoverage，不知道哪条路径是死的。
3. **`smoke_tests.cpp` 是厨房水槽。** 数学、IO、命令、BIM 挤在一个 1600+ 行文件里，失败时难定位，也不利于按模块并行。
4. **可跳过的插件加载不是门禁。** `LoadsManagedHelloCommands` 缺 CLR 就 skip，CI（即便以后加上）也可能从不真正加载托管插件。

### 渲染与视觉

5. **没有像素级回归。** Mock RHI 能证明「会提交这些 draw」，不能证明「屏幕上长什么样」。离屏 PNG + 金样图后置，见 [RENDER-SCENE.md](RENDER-SCENE.md)。
6. **Pin 金样目录约定已落地。** 完整夹具是 `assets/samples/render/<name>/{scene.trscn,scene.meta.json}`；裸 `.trscn` 不算金样。像素对比仍未做。
7. **三个 RHI 后端都没有设备测试。** 创建 swapchain / 上传网格 / 编译 shader / `draw_channel` 真实路径未覆盖。WebGL 线框降级成实体着色也无人断言。

### 格式与造型

8. **GLB / STEP / IGES / BREP 导入已有 headless 测试。** ASCII glTF 仍不支持（断言拒绝）。仓库仍无检入的 CAD 样例文件（测试里临时写盒子再读回）。
9. **IFC 只测空间树。** 几何（IfcGeom）未接线，大模型 / GUID / Pset 未测。
10. **布尔并/交/差已覆盖**（特征树 + `boolean` 命令）。
11. **原先缺 dispatch 的命令已补**（梁、柱、圆柱、门、弧、矩形、楼层、`set_location`、`set_material`、`chamfer`）。

### 壳与其它语言

12. **Qt UI 零覆盖。** 快捷键、Ribbon 启用态、属性面板绑定、主页最近文件，全靠手工点。
13. **Web 零覆盖。** `web/package.json` 只有 `dev` / `build` / `preview`，无 test script；WASM 构建不编 gtest。
14. **C# SDK 零覆盖。** 契约解析、ALC 加载、异常吞掉后的 Log，没有托管测试项目。
15. **相机手势。** `CameraController` 的 orbit / pan / frame / dolly_to_focus 已有单测；桌面/Web 壳如何把鼠标映射过去仍未测。
16. **性能 / 大规模。** [超大规模三角](MASSIVE-GEOMETRY.md) 没有基准或超时测试。

---

## 6. 以后加测试时怎么放

- **内核、无窗口**：继续放 `tests/*.cpp`，链现有 `tamias_tests`。一类行为一个文件，不要再往 `smoke_tests.cpp` 堆。
- **渲染回归**：先锁 `.trscn` digest（仍无 GPU）；像素对比等离屏 RHI。
- **插件 C++**：`HostApi` 继续纯 C++ 测；需要 CLR 的用例标清楚依赖，避免默默 skip。
- **C# / Web / Qt**：各自用该生态的跑法（xUnit、vitest、Qt Test），不要硬塞进 gtest。
- **CI**：至少 Windows 或 Linux 一条 `cmake --build` + `ctest --output-on-failure`；插件 CLR 测试单独 job，缺运行时就显式 skip 而不是假装绿。

---

## 7. 缺口怎么测

原则：**能在无窗口 `gtest` 里锁住的，先锁。** 需要窗口、GPU、浏览器、CLR 的，单独目标、CI 可 skip，不要绑进默认 `ctest` 让没显卡的机器变红。

| 现在就能写（仿现有用例） | 要先改产品代码才能自动化 |
|---|---|
| 缺的 `dispatch`、布尔交/差、`chamfer` 命令 | IFC **几何**（IfcGeom 未接线） |
| `CameraController`、`Session` 事件 | GPU 像素 / 离屏 PNG（RHI 还没有 readback） |
| GLB / STEP / IGES / BREP 导入（加小夹具） | LevelDB `.tdoc`、超大规模流式（功能未落地） |
| `.trscn` 金样 digest | Qt 整窗点击（MainWindow 太厚，见下） |
| C# `CommandArgs` 文本格式 | Playwright 真打开 WASM 画布 |

### 7.1 缺的命令：`dispatch` + 撤销

不要只调 `Entity::createGeom()`。命令测的是：**参数进注册表 → 实体进 Document → undo 能撤干净**。模板就是已有的 `CommandSystem.DispatchCreateWallFromPoints` / `DispatchCreateBoxFromOrigin`。新文件建议 `tests/command_dispatch_tests.cpp`，不要再塞 `smoke_tests.cpp`。

| 命令 | 怎么喂 | 断言 |
|---|---|---|
| `create_beam` | 与墙相同：`points` 两个点，或 `feed_point` 两次 | 1 个 `EntityKind::Beam`，有网格；undo 后实体和网格都空 |
| `create_column` / `create_cylinder` / `create_door` | 与盒子相同：`origin` 一个点（`make_primitive`） | kind 对、`createGeom` 已有覆盖不必重复三角数 |
| `create_arc` / `create_rectangle` | 与线/圆相同：`points` 或多次 `feed_point` + 必要时 `confirm` | 草图实体、非 Family |
| `create_storey` | `name` + `elevation`，立刻完成（非交互） | `doc.bim().storeys()` 多一条；undo 删掉 |
| `set_location` | 先 `create_storey` + 建柱/板，再 `entity_id` / `storey_id` / `elevation_offset` | 与 `Location.SlabUsesStoreyRelativeElevation` 相同，但走命令栈 |
| `set_material` | 先建盒子，再 `entity_id` + `name` + `base_color` | `entity->material_id` 变了；undo 恢复旧 id；**不**重建网格 |
| `chamfer` | 抄 `CommandSystem.AddFeatureUndoRedo`，命令名改 `chamfer`，参数 `distance` / `edge` | 特征数 +1；undo 回到挤出 |

交互式命令：没喂点时 `entities().size()==0` 且 `has_pending()`。脚本式带 `points`/`origin` 时 `has_pending()==false`。

### 7.2 布尔交 / 差

两层都要，现有只锁了并（`operation == 0` = `BooleanOp::Fuse`）。

**特征树**（抄 `FeatureModel.BooleanUnionAddsGeometry`）：同一套 RectProfile+Extrude 盒子 ∪ 圆柱，把 `{"operation", 0.0}` 改成 `1.0`（Common）和 `2.0`（Cut）。断言：

- Fuse：三角数 **大于** 单盒
- Cut：包围盒或体积变小（`bounds` 某一轴缩短，或 indices 仍非空但 `max` 小于并）
- Common：网格非空，包围盒落在两体相交区

**命令**（抄 `CommandSystem.BooleanUndoRedo`）：两个错开的盒子（第二个 `origin` 不要叠在同一个点上，否则 Cut 可能空壳），`operation` 用 `1` / `2`。Cut 后实体数仍为 1；undo 回到 2。空结果要 `ASSERT_TRUE(r) << r.error()`，失败时看 OCCT 报错而不是静默。

### 7.3 相机手势（无 Qt）

`CameraController` 已是纯 C++（[`camera_controller.h`](https://github.com/terry-chao/tamias/blob/main/src/host/camera_controller.h)），壳只把鼠标增量转进来。新文件 `tests/camera_controller_tests.cpp`：

```cpp
CameraController cam;
const Vec3 eye0 = cam.camera().eye();
cam.orbit(100.f, 0.f);                 // 像素增量，内部 × 0.01
EXPECT_NE(cam.camera().eye().x, eye0.x);
cam.pan(50.f, 0.f);
cam.frame_aabb(box);
EXPECT_GT(cam.camera().distance(), 0.f);
```

再测 `dolly_to_focus`：focus 点投影到屏幕的位置在 dolly 前后应接近（用已有 `Picking.ProjectWorldToScreenCenter` 那套投影）。**不要**在测试里 new `QWidget`。桌面/Web 手感对齐靠这个数学锁住，壳只负责「中键=pan」。

`Session` 事件：`set_listener` 记 `HostEvent` 序列，`select` / `dispatch`+`feed_point` / `set_tool` 之后分别出现 `SelectionChanged`、`DocumentChanged`、`ToolChanged`。

### 7.4 GLB / STEP / IGES / BREP

都走现有边界，不启动 Qt。夹具放 `assets/samples/`，测试用 `TAMIAS_SOURCE_DIR`（与 IFC 空间树相同）。

**GLB。** `load_gltf` / `load_glb` 只认三角化、带 POSITION 的二进制 glTF。做一个最小三角的 `.glb` 入库（或测试里用已知小文件）。断言 `indices` 非空、`bounds.valid()`。再加一条：把 ASCII `.gltf` 喂进去，期望 `Result` 失败（产品本来就不支持）。坏 magic 抄 `RenderSceneIo.RejectsBadMagic`。

**STEP / IGES / BREP。** 不要点「打开文件」对话框。直接：

```cpp
register_occt_shape_ops();  // 若测试进程尚未注册
auto* ops = ShapeOpsRegistry::instance().find("occt");
ASSERT_NE(ops, nullptr);
auto shape = ops->read_file(path);   // .step / .iges / .brep
ASSERT_TRUE(shape) << shape.error();
auto mesh = (*shape)->tessellate(0.1);
ASSERT_TRUE(mesh);
EXPECT_FALSE(mesh->indices.empty());
```

夹具从哪来：用 OCCT 写一个盒子 `.brep` 进仓库（体积小）；STEP/IGES 各留一个从该盒子导出的文件，避免每次测都依赖写盘权限。缺文件时 `GTEST_SKIP` 不如 **ASSERT 失败**——夹具是仓库的一部分。`occt_supports_extension` 对 `.obj` 应返回 false，可顺手测。

打开进 Document 的路径是 `add_import_mesh`（无特征树）：load 成功后 `doc.add_import_mesh(name, *mesh, …)`，再 `render_items()` 条数 ≥ 1。这锁的是「导入网格进场景」，不是 OCCT 读文件本身。

### 7.5 IFC 几何

**现在不要写「IFC 三角网」测试。** [`ifc_spatial_tree.h`](https://github.com/terry-chao/tamias/blob/main/src/bim/ifc_spatial_tree.h) 写明 IfcGeom 未用。几何接线之前，自动化只能继续锁空间树（已有 `IfcSpatialTree.SampleProjectSiteBuildingStoreyWall`）。

接线之后：同一份 `spatial-tree.ifc`（或带实体的样例）→ IfcGeom 出 `MeshCpu` → `add_import_mesh` 或 BIM 实体 → `render_items` 非空。GUID / Pset 是下一层，不要和「能不能画出墙」绑在一个 TEST 里。

### 7.6 `.trscn` 金样（无 GPU，现在就能做）

这是渲染回归的**第一层**，做法在 [RENDER-SCENE.md §4](RENDER-SCENE.md#4-结合自动化测试)。手工调试顺序见 [调试步骤](RENDER-SCENE.md#调试步骤)。应用里 **Pin Render Scene for Tests** 把当前视口写到 `assets/samples/render/<name>/`（`scene.trscn` + `scene.meta.json`）。gtest `RenderSceneGolden.ScansRepositoryFixtures` 扫这个目录：digest、hydrate、Mock draw。没有夹具时 skip。

着色或烘焙改了导致 digest 变：再 Pin 覆盖金样，不要改哈希函数去凑绿。Web 打开 `.trscn` 仍是人眼，不要假装进了 gtest。

### 7.7 GPU / 像素（分三层，别一步到位）

```
① .trscn digest + Mock draw     ← 已有（Pin → assets/samples/render/）
② 真设备：create + upload + 一帧不崩
③ 离屏 readback → PNG 金样     ← 要先给 RHI 加读回
```

**② 真设备。** 新目标（例如 `tamias_rhi_smoke`），**不要**默认 `gtest_discover_tests` 进每台机器的 `ctest`。流程：`RHIDevice::create({Vulkan})`，失败则 `GTEST_SKIP`（无驱动 / CI 无 GPU）；`create_swap_chain` 目前要 `NativeWindowHandle`，所以这一步要么藏一个隐藏窗，要么先给 Device 加「无窗离屏表面」——**没这 API 之前不要写像素测试**。OpenGL / Vulkan 各一条；WebGL 只在 Emscripten 里有，桌面 gtest 测不了。

**③ 像素。** 等 Device 能画到离屏纹理并能 `read_pixels`。然后：加载 `box.trscn` → 固定相机 → 读 64×64 RGBA → 与 `assets/samples/box.png` 比（允许少量 RMSE，各 GPU 精度不同）。WebGL 线框会画成实体，金样必须按后端分，或这条只锁着色模式。

没有 readback 之前，用 RenderDoc 手工抓一帧，不要为了「有测试」去比窗口截图（DPI、装饰、抗锯齿都会抖）。

### 7.8 Qt 壳

不要对 `MainWindow` 做点击脚本，它绑着视口、插件、文件对话框。拆开测：

| 能测 | 怎么测 |
|---|---|
| 已抽出的逻辑 | `ViewportFloor` 已在 gtest；同类（纯函数 / 无窗）继续 gtest |
| `RecentFilesStore` | 需要 `QCoreApplication`，用 Qt Test 或在 gtest 里 `QCoreApplication app(argc, argv)` + 临时 `QSettings` 路径；断言 add 后上限 8、去重、remove |
| i18n | 加载 `.qm`，抽几条 `QCoreApplication::translate`，不要测整窗布局 |
| Ribbon 启用态 | 若逻辑是「`can_undo` → 按钮 enabled」，测 `Session::can_undo()` 即可，不必找 QAction |

整窗快捷键、主页拖放：暂时用一页手工清单（教程第 3 章已经带「当用户」）。真要自动化再加 `Qt6::Test` + `QTest::keyClick`，单独 `tamias_qt_tests`，headless 用 `QT_QPA_PLATFORM=offscreen`（Linux CI 可行；Windows 仍重）。

### 7.9 Web / WASM

三层，别一上来 Playwright 开 GPU 画布：

1. **会话语义（已有桌面 gtest）。** `ViewerHost` 的 dispatch / undo / 选择就是 `Session`。桌面 `SessionTest` 绿了，embind 只是转发；缺的是「TS 类型和 C++ 导出不同步」——改 `viewer_main.cpp` 时对照 [`web/src/viewer.ts`](https://github.com/terry-chao/tamias/blob/main/web/src/viewer.ts)。
2. **React 壳。** `web/` 加 vitest + Testing Library：测「打开按钮 / 拖放把文件交给 mock ViewerHost」「状态栏显示 status」。**不**加载 `.wasm`。
3. **真浏览器。** Playwright 等 wasm 构建产物：打开预览 URL，拖一个 `.trscn`，断言 canvas 存在、status 不是 error。像素对比同样后置。Emscripten 预设关闭 `TAMIAS_BUILD_TESTS`，不要试图在 wasm 里编 gtest。

### 7.10 C# 插件 SDK

C++ 已经测 HostApi 和（可选）加载 Hello。C# 侧缺的是**文本协议**，和 [`command_arg_text`](https://github.com/terry-chao/tamias/blob/main/src/host/command_arg_text.h) 对得上。

新建 `plugin-sdk/csharp/Tamias.Api.Tests`（xUnit），**不要**在测试里启动 `tamias.exe`：

- `new CommandArgs().SetInt("entity_id", 3).SetDouble("thickness", 0.2).ToString()` 等于 C++ 能 parse 的串（与 `CommandArgText.ParsesTypedAndInferredValues` 同一批样例）。
- 非法串、空 points。

`Tamias.Host` 的 ALC / `Bootstrap.Invoke` 吞异常：继续用现有 `PluginHost.LoadsManagedHelloCommands`；CI 要这条作门禁就把 skip 改成失败，job 标明需要 .NET 运行时。插件业务（Hello 删选择）通过 HostApi `dispatch("delete_entity")` 在 C++ 测，不必用 UI 自动化点 Ribbon。

### 7.11 CI、覆盖率、性能

**CI。** 新 workflow（别塞进 `pages.yml`）：checkout → vcpkg 缓存 → `cmake --preset` → `--target tamias_tests` → `ctest --output-on-failure`。Windows 要 VS + Qt + Vulkan SDK，成本高；Linux 预设更像第一刀。GPU / CLR 分 job，失败用 skip 输出，不要整条流水线红。文档站点继续只 `mkdocs`。

**覆盖率。** 本机：Windows 用 OpenCppCoverage 包一层 `tamias_tests.exe`；Linux 用 `--coverage`。先看 `src/command`、`src/engine/document`、`src/engine/io`，不要拿百分比卡 PR。Qt / RHI 覆盖率会长期很低，属正常。

**性能。** 功能未做「几十亿三角」之前，只锁**不会偷偷退化**的规模：循环 `create_box` 1000 次，`render_items(&frustum)` 设耗时上限，且返回条数应少于实体总数（视锥剔除）。用 `std::chrono`，机器差就 `GTEST_SKIP` 或放宽阈值。不要在 CI 比 FPS。

### 7.12 建议动手顺序

1. `command_dispatch_tests.cpp`（缺的命令 + chamfer + 布尔 1/2）——纯仿现有，当天能绿。
2. `camera_controller_tests.cpp` + Session listener。
3. 桌面 **Pin Render Scene for Tests** 入库（`assets/samples/render/<name>/`），跑 `RenderSceneGolden*`。
4. 最小 `.glb` + 一个 `.brep` 盒子。
5. C# `CommandArgs` xUnit（与 C++ 样例同源）。
6. CI 跑 `ctest`（先 Linux 或本机自托管）。
7. RHI 离屏 readback 落地后再谈 PNG。
8. IfcGeom 接线后再谈 IFC 网格。

相关：[构建](https://github.com/terry-chao/tamias/blob/main/BUILD.md)、[架构](ARCHITECTURE.md)、[渲染场景快照](RENDER-SCENE.md)、[插件开发](plugin/develop.md)、[Web 查看器](WEB.md)。
