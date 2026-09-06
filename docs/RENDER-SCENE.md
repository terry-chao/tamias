# 渲染场景快照（`.trscn`）

> **v1 已落地。** 落盘的是 CPU 侧已烤好的 `MeshCpu` + `SceneDrawItem` + 相机 + 被引用贴图。用来复现场景、调渲染、喂自动化测试。不是 `.tdoc`（无实体/特征），也不是 RenderDoc（无 GPU 命令流）。

卡在 `Document::render_items()` 之后、`upload_mesh` 之前。管线总述见 [管线与 RHI](RENDERING.md)；展平见 [语义树](SCENE-GRAPH.md)。

实现：[`render_scene.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_scene.h)、[`render_scene.cpp`](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_scene.cpp)、[`render_scene_io.cpp`](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_scene_io.cpp)。测试：[`tests/render_scene_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/render_scene_tests.cpp)。

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

### 桌面：导出

1. 打开 `.tdoc` / STEP / OBJ 等，把视口转到要留下的角度和着色模式（线框 / 着色 / 真实感）。
2. 功能区 **Home → File → Export Render Scene**。
3. 存成 `某个名字.trscn`。

导出成功后弹出 inspect 文本（网格数、贴图数、`digest=`、每条 draw item）。这是调试摘要，文件本身已经写到磁盘。

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

打开快照后 `render_items()` 走快照清单（保留粗糙度、金属度、贴图 id），只同步选中态。水合时用 `replace_textures` 换掉文档构造函数种下的默认材质贴图，避免把 10 张 512² 默认纹理全部 upload。

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

API：`bake_render_scene`、`render_scene_digest`、`inspect_render_scene`、`serialize` / `deserialize` / `save` / `load`、`is_render_scene_path`。文档侧：`Document::capture_render_scene`、`set_render_snapshot`、`document_from_render_scene`。

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

### 推荐：桌面导出 → 仓库金样 → 断言 digest

手工拼 `handmade_scene()` 适合测 IO。**锁真实模型**用导出的 `.trscn`，测试里不必再走 OCCT / 特征树。

1. 桌面打开模型，转到要锁住的视角和着色模式。
2. **Export Render Scene**，把文件放进仓库，例如 `assets/samples/box.trscn`（和 IFC 样例一样，测试里用 `TAMIAS_SOURCE_DIR`）。
3. 测试里加载金样，比 digest / 条数，必要时再 mock 录制一遍：

```cpp
TEST(RenderSceneGolden, BoxSnapshot) {
  const auto path =
      std::filesystem::path(TAMIAS_SOURCE_DIR) / "assets" / "samples" / "box.trscn";
  auto scene = load_render_scene(path);
  ASSERT_TRUE(scene) << scene.error();
  EXPECT_EQ(render_scene_digest(*scene), "把导出对话框里那行 digest= 粘过来");
  EXPECT_FALSE(scene->items.empty());

  Document doc = document_from_render_scene(*scene);
  EXPECT_EQ(doc.render_items().size(), scene->items.size());
}
```

导出弹窗里的 inspect 文本就有 `digest=`。烘焙或着色改了、digest 对不上，不是文件坏了，是场景内容变了：要么更新金样，要么查回归。

### 三层分别锁什么

```
.tdoc / STEP     编辑、求值          → 现有 DocumentIo / 建模测试
.trscn           烤完的 CPU 场景     → digest + mock 录制（无窗口）
PNG / 截图       GPU 像素            → 还没做，需要离屏 RHI
```

现在自动化停在第二层：能证明「这份场景会提交这些 draw」，不能证明「像素和某张图一样」。Web 打开 `.trscn` 是人眼看，没有进 gtest。离屏 PNG 视觉回归后置。
