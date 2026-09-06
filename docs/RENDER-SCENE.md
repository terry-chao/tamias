# 渲染场景快照（`.trscn`）

> **v1 已落地。** 落盘的是 CPU 侧已烤好的 `MeshCpu` + `SceneDrawItem` + 相机 + 被引用贴图。用来复现场景、调渲染、喂自动化测试。不是 `.tdoc`（无实体/特征），也不是 RenderDoc（无 GPU 命令流）。

卡在 `Document::render_items()` 之后、`upload_mesh` 之前。管线总述见 [管线与 RHI](RENDERING.md)；展平见 [语义树](SCENE-GRAPH.md)。

实现：[`render_scene.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_scene.h)、[`render_scene.cpp`](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_scene.cpp)、[`render_scene_io.cpp`](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_scene_io.cpp)。测试：[`tests/render_scene_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/render_scene_tests.cpp)。

---

## 这个工具在调试什么

**不是**断点调试器，也**不是**查 GPU / shader。Pin / Export 冻住的是：**这一帧交给渲染器的那份 CPU 货单**——有哪些网格、每条 draw 用什么材质/贴图、相机在哪。

屏幕上一个盒子，背后其实是两段活：

1. **建模**（`.tdoc`）：墙、布尔、OCCT 求值 → 三角网
2. **渲染**：把三角网上 GPU 画出来

Pin 卡在中间：建模已经算完、GPU 还没上场。它回答的问题是：

> 「程序到底打算画什么？」而不是「像素为什么是这个颜色？」

典型用途：

| 你怀疑的问题 | Pin 能不能帮上 |
|---|---|
| 盒子三角数不对、缺面、包围盒飞了 | 能。inspect 里看 `meshes` / `tris` / `aabb` |
| 颜色、贴图、金属度烘焙错了 | 能。看 item 的 color / albedo / normal |
| draw list 少了构件、多了隐藏物体 | 能。看 `items=` |
| 改完渲染代码，场景内容偷偷变了 | 能。digest 金样会红 |
| shader、深度、光照、锯齿 | **不能。** 用 RenderDoc |
| 墙连接、特征树、布尔求值错 | **不能直接查。** 继续用 `.tdoc` |

所以它调试的是 **CPU 侧场景烘焙 / 展平**（`render_items()` 的输出），外加把「这一版输出」锁进测试，防止以后无声回归。

手工怎么走见 [调试步骤](#调试步骤)。

---

## 0. 和 `.tdoc` 怎么选

| | `.tdoc` | `.trscn` |
|---|---|---|
| 干什么 | 编辑、保存特征 | 调渲染、复现场景、测试夹具 |
| 有没有实体 / 特征 | 有 | 没有 |
| 下次打开 | 走文档管线（可重算） | 直接喂烤好的网格和 draw list |
| 日常建模 | 用这个 | 不要当工作文档 |

日常继续用 `.tdoc`。某一帧看起来不对、想固定那一版网格和相机再查，再导出 `.trscn`。

只会带上**当前能画到的**网格，以及 item 的 albedo/normal 真正引用到的贴图。文档里默认那堆 512² 材质如果没被引用，不会打进文件。

---

## 1. 怎么用

### Export 和 Pin 怎么选

| | **Export Render Scene** | **Pin Render Scene for Tests** |
|---|---|---|
| 入口 | Home → File → Export | Home → File → Pin（`Ctrl+Shift+P`） |
| 写到哪 | 你选的任意 `.trscn` | 固定 `assets/samples/render/<name>/` |
| 用途 | 临时复现：写盘并立刻打开只读快照 | 入库金样，gtest 会扫 |
| 会不会改当前文档路径 | 否 | 否（Save 仍写原来的 `.tdoc`） |

日常建模继续用 `.tdoc`。某一帧看起来不对、想固定网格和相机再查，再 Export / Pin。

### 桌面：导出

1. 打开 `.tdoc` / STEP / OBJ 等，把视口转到要留下的角度和着色模式（线框 / 着色 / 真实感）。
2. 功能区 **Home → File → Export Render Scene**。
3. 选一个文件名（`某个名字.trscn`）。

写盘后**立刻在新标签打开**这份快照：相机和着色模式从文件恢复，状态栏写 `read-only draw list`。原来的 `.tdoc` 标签还在，Save 仍写原来的文档。状态栏会带 `digest=`。

### 桌面：钉进测试（Pin）

调试到「这一帧就是对的」之后：**Home → File → Pin Render Scene for Tests**（`Ctrl+Shift+P`）。

1. 起一个英文夹具名（字母开头，只含字母数字 `-` `_`），例如 `box`。
2. 写入仓库 `assets/samples/render/<name>/`：
   - `scene.trscn`
   - `scene.meta.json`（digest、条数；测试读这个，不要把哈希写进 C++）
   - `scene.inspect.txt`（全量人读 dump）
   - `debug/`（OBJ 网格 + PPM 贴图）
3. 写完立刻再 load 一遍，digest 对不上会失败。
4. **不会**改当前文档路径（Save 仍写原来的 `.tdoc` / `.trscn`）。
5. 写完立刻跑 `tamias_tests --gtest_filter=RenderSceneGolden*`。旁边没有 `tamias_tests.exe` 时，先调 `scripts/build-tests.ps1` 按当前配置（Debug / RelWithDebInfo / Release）编出来。结果接在 inspect 对话框后面。

已有同名夹具会问是否覆盖。不要把金样存到 `build\bin\...`。

`ScansRepositoryFixtures` 只收录同时有 `scene.trscn` 和 `scene.meta.json` 的子目录。只有裸 `.trscn` 会被忽略；仓库里还没有任何完整夹具时这条 skip，不算失败。`IncompleteRepoFixturesAreRejected` 会把这种半成品夹具标红，避免扫描 skip 造成假绿。

### Pin 之后怎么看数据

Pin / Export 会写出二进制快照，再附上**全量人读 dump** 和 **可视化 sidecar**。打开 `.trscn` 时左侧会弹出 **Render Scene** 面板（`Ctrl+Shift+I`）：点一条 draw，视口画黄色 AABB；勾选 Isolate 只留该条。

| 文件 | 干什么 |
|---|---|
| `scene.trscn` | 二进制快照。用软件 **Open** 打开，眼睛看那一帧长什么样 |
| `scene.inspect.txt` | **全量**人读 dump：相机矩阵、每条 draw 的 transform / PBR、全部顶点与索引、贴图像素摘要 |
| `debug/mesh_<id>.obj` | 资产空间网格，Blender / 任意 DCC 可打开 |
| `debug/draw_*_node_*.obj` | 该条 draw 的世界空间拷贝（已乘 transform） |
| `debug/tex_<id>.ppm` | albedo / 法线贴图像素，系统看图软件可开 |
| `scene.meta.json` | 给测试用的数字：`digest`、`items`、`meshes` |

Export 写在 `.trscn` 旁边：`foo.inspect.txt`、`foo.debug/`。

**看画面：** File → Open → `assets/samples/render/box/scene.trscn`。和 Pin 当时一样就对了。面板里点 draw 对包围盒。

**看数字：** 打开同目录的 `scene.inspect.txt`。开头仍是计数和 digest，下面是 VIEW / MESHES / TEXTURES / DRAWS。

对照你的预期：该有 1 个盒子 → `items=1`、`tris=12`；盒子该在原点附近 → aabb 大约 `(-0.5,0,-0.5)-(0.5,1,0.5)`。条数不对、aabb 飞了、缺 albedo，问题就在这份货单里。

**看有没有悄悄变：** Pin 写完会自动跑 `RenderSceneGolden*`，输出在同一个对话框里。也可以自己跑 `tamias_tests --gtest_filter=RenderSceneGolden*`。测试重读 `scene.trscn`，再算 digest，和 json 里记下的比。红了再打开 inspect，看是条数变了还是网格变了。

要查 shader / 像素用 RenderDoc；要查墙怎么连用原来的 `.tdoc`。

### 调试步骤

Pin 卡在 `render_items()` 之后、`upload_mesh` 之前，用来锁「这一帧 CPU 侧该画什么」。不是 RenderDoc，也不是 `.tdoc`。

**1. 桌面里复现**

打开模型（`.tdoc` / STEP / OBJ），转到要锁的视角和着色模式（线框 / 着色 / 真实感）。确认这一帧就是你要对照的状态。

**2. Export：写盘并打开快照**

Home → File → **Export Render Scene**，选一个文件名。写完立刻在新标签打开这份 `.trscn`，不用再走 File → Open。

- 原来的 `.tdoc` 标签还在
- 快照相机会恢复，状态栏写 `read-only draw list`
- 打开后不再走特征树 / OCCT；画面还错，问题在渲染；画面对了，问题在建模 / 烘焙之前

**3. 看对了再 Pin 入库**

`Ctrl+Shift+P`（或 Home → File → **Pin Render Scene for Tests**），夹具名例如 `box`。状态栏会显示 `Pinned golden ... digest=...`。

**4. 对照 inspect 与可视化 sidecar**

`assets/samples/render/box/scene.inspect.txt` 现在是全量 dump（顶点、矩阵都在）。`debug/mesh_*.obj` 和 `debug/tex_*.ppm` 用外部工具看网格和贴图。软件里 `Ctrl+Shift+I` 打开 Render Scene 面板，点 draw 看黄色包围盒。

先看：item 条数对不对、aabb 是否离谱、贴图 id 是否被引用、digest 是否和上次一样。

**5. 跑金样测试（无窗口、无 GPU）**

```
cmd /c "call `"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`" && cmake --build --preset debug --target tamias_tests"
.\build\bin\Debug\tamias_tests.exe --gtest_filter=RenderSceneGolden*
```

Windows 终端必须先 `vcvars64.bat` 再编，见仓库 `.cursor/rules/windows-msvc-build.mdc`。

**6. digest 变了怎么判**

| 情况 | 做法 |
|---|---|
| 你故意改了烘焙 / 着色 | 再 Pin 覆盖金样 |
| 没改场景，digest 却变了 | 查回归，不要改哈希函数去凑绿 |
| 只改了相机 | digest **不含**相机，测试不会因转视角红 |

按层定位（对应下面 [§2](#2-卡在管线哪)）：

| 现象 | 查哪一层 |
|---|---|
| Pin 绿、屏幕仍错 | GPU / shader / RHI（用 RenderDoc） |
| 打开 `.trscn` 就错 | 烘焙、材质、draw item |
| 打开 `.tdoc` 错、打开 `.trscn` 对 | 特征求值 / 网格生成 |

现在自动化只锁到「这份场景会提交这些 draw」，没有像素级 PNG 对比。Web 打开 `.trscn` 仍是人眼看。

### 桌面：打开与保存

和打开普通文件一样：

- **Home → File → Open**，筛选器选 **Render Scene (*.trscn)**，或 All Supported 里直接点 `.trscn`
- 欢迎页最近文件里点它

打开后会恢复当时的相机和渲染模式。状态栏会写 `read-only draw list`：这是展平后的绘制清单，不能当 `.tdoc` 继续建模。

当前这份快照上 **Save** 写回同一个 `.trscn`。**Save As** 按所选扩展名：`.trscn` 仍是快照，`.tdoc` 会另存工作文档（几何/材质可有损）。

### Web 查看器

```
cmake --build --preset wasm-serve
```

浏览器顶栏 **打开 .tdoc / .trscn / .obj**，或把 `.trscn` 拖进视口。相机和着色模式从文件恢复。详见 [Web 查看器](WEB.md)。

---

## 2. 卡在管线哪

```
特征 / 导入 / BRep
    → MeshCpu
        → Document::render_items()     SceneDrawItem 展平
            → ★ .trscn 落盘（bake + 序列化）
                → upload_mesh / upload_texture
                    → 场景图录制
                        → GPU / RenderDoc
```

打开快照后 `render_items()` 走快照清单（保留粗糙度、金属度、贴图 id），只同步选中态。水合时用 `replace_textures` 换掉文档构造函数种下的默认材质贴图，避免把 10 张 512² 默认纹理全部 upload。按层排查见上面 [调试步骤](#调试步骤) 第 6 步。

Windows 上路径一律 UTF-8（`path_to_utf8` / `qstring_to_path`）。不要用 `QString::toStdString()` 再塞进 `std::filesystem::path`，中文目录会抛 `std::system_error`。

---

## 3. 文件格式（v1）

Magic `TRSC`，version 1。Chunk：`META` / `VIEW` / `MESH` / `DRAW`；有贴图时再加 `TEXT`。未知 chunk 会跳过，旧的四 chunk 文件仍能读。

| Chunk | 内容 |
|---|---|
| `META` | 来源名、网格/item/贴图数量、三角数、digest（信息性） |
| `VIEW` | 矩阵 + 转盘相机参数 + `RenderMode` + 宽高 |
| `MESH` | 被引用的 `MeshCpu`（与 `.tdoc` 同一套 `mesh_binary`） |
| `DRAW` | 已烘好的 `SceneDrawItem` |
| `TEXT` | 被 albedo/normal 引用到的 `TextureAsset`（RGBA8） |

API：`bake_render_scene`、`render_scene_digest`、`inspect_render_scene`、`write_render_scene_debug_files` / `write_render_scene_debug_sidecars`、`serialize` / `deserialize` / `save` / `load`、`is_render_scene_path`。文档侧：`Document::capture_render_scene`、`set_render_snapshot`、`document_from_render_scene`。

---

## 4. 结合自动化测试

`.trscn` 就是给测试用的夹具：把烤好的网格、draw list、相机冻住。CI 里不用开 Qt，也不用 GPU。

### 已经接上的

`tamias_tests` 里 `RenderScene*` / `RenderSceneIo*` 覆盖：

| 测什么 | 怎么测 |
|---|---|
| 格式 | 序列化 roundtrip，digest 不变 |
| 烘焙 | 只带被引用的网格 / 贴图 |
| 水合 | `document_from_render_scene` 后材质、贴图还在 |
| 录制 | `build_scene_graph` + Mock RHI，断言 draw 次数、颜色、矩阵 |
| 路径 | 含中文的目录 roundtrip 不抛异常 |

`render_scene_digest` 是对网格、贴图像素、draw item（含 roughness / 贴图 id）的 FNV 哈希，**不含**相机。同一场景写两遍文件，digest 应相同；改一个顶点或贴图像素，digest 应变。

跑：

```
cmd /c "call `"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`" && cmake --build --preset debug --target tamias_tests"
.\build\bin\Debug\tamias_tests.exe --gtest_filter=RenderScene*
```

Windows 终端必须先 `vcvars64.bat` 再编，见仓库 `.cursor/rules/windows-msvc-build.mdc`。

### 推荐：Pin → 仓库金样 → 扫描测试

手工拼 `handmade_scene()` 适合测 IO。**锁真实模型**用 Pin，测试里不必再走 OCCT / 特征树，也不用手写一条 `TEST`。桌面里怎么走到 Pin，见 [调试步骤](#调试步骤)。

1. 桌面打开模型，转到要锁住的视角和着色模式。
2. **Pin Render Scene for Tests**，夹具名例如 `box`。
3. 提交 `assets/samples/render/box/`（`scene.trscn` + sidecar）。
4. `tamias_tests --gtest_filter=RenderSceneGolden*`：digest 对 sidecar、hydrate 条数、Mock draw 次数。

digest 变了：不是文件坏了，是场景内容变了。故意改烘焙就再 Pin 覆盖；没改却变了就查回归。**不要**改哈希函数去凑绿。

### 三层分别锁什么

```
.tdoc / STEP     编辑、求值          → 现有 DocumentIo / 建模测试
.trscn           烤完的 CPU 场景     → digest + mock 录制（无窗口）
PNG / 截图       GPU 像素            → 还没做，需要离屏 RHI
```

现在自动化停在第二层：能证明「这份场景会提交这些 draw」，不能证明「像素和某张图一样」。Web 打开 `.trscn` 是人眼看，没有进 gtest。离屏 PNG 视觉回归后置。
