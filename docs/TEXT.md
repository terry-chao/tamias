# 场景文字：怎么引入、怎么管、怎么画

> 状态：**P0 + P1 + P2 + P3 已落地**（文字核 / 字体与图集 / 屏幕空间渲染通路 / 派生标注 / 可建可改可撤销的文字注记 + `.tdoc` 持久化），P4 未做。本文定「文字从哪来、存在哪、怎么变成屏幕上的字」，落地拆成 P0–P4（见 §8）。
>
> 一句话结论：**文字不是几何，别塞进 `SceneDrawItem`。** 走两条通路——屏幕空间标注用 GPU 文字 pass（billboard），模型空间文字先变成三角网再当普通 draw item；两者共用一份 engine 侧、Qt-free 的文字核。

---

## 0. 结论先行

| 问题 | 结论 |
|---|---|
| 文字能塞进 `SceneDrawItem` 吗？ | **不能。** 那条路会进 `mesh.frag` 做 PBR 光照，字形会被当三角面打光、贴图槽也会打架。文字要自己的管线。 |
| 有几种文字？ | 三种，别混：**屏幕空间标注**（轴号 / 标高 / 尺寸 / 标签）、**模型空间文字**（平面图房间名 / 图框标题 / 刻进楼板的字）、**图纸文字**（DXF/DWF 里带进来的）。 |
| 各走哪条路？ | 屏幕空间 → **新 GPU 文字 pass**（实例化四边形 + 字形图集）。模型空间 → **轮廓三角化 → `intern_mesh()` → 普通 `SceneDrawItem`**（零新渲染代码）。图纸文字 → 解析成 `TextItem` 后并进前两条。 |
| 共享地基是什么？ | engine 侧 `engine/render/text/`：字体解析（vendored `stb_truetype.h`）、字形图集、布局（折行 / 对齐 / 行高）。**Qt-free**，桌面和 wasm 同一份。✅ 已落地 |
| 派生标注要落盘吗？ | **不落盘。** 轴号 / 标高 / 尺寸由 `BimModel` 现算，每帧生成。自由文字才是实体，进 `.tdoc`、走命令撤销。 |
| 「如何引入文字」 | 四条入口：① 数据派生（轴号 / 标高，最快见效）② 交互创建（注释 → 文字命令）③ 导入（DXF/DWF/IFC）④ API（插件 / wasm）。见 §6。 |

---

## 1. 三类文字，和现状一条条对

| 类别 | 例子 | 今天在哪 | 缺什么 |
|---|---|---|---|
| **屏幕空间标注** | 轴网编号 `A` / `1`、标高 `±0.000`、尺寸链、构件名 | 轴网只画线（[document_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/viewport/canvas/document_viewport.cpp) `submit_current_frame()` 里 `append_axis_segments`，`GridAxis::name` 有值但没人画）；屏幕上的文字只有 QWidget 叠加层（ViewCube、坐标读出） | 完全没有文字渲染通路 |
| **模型空间文字** | 平面图房间名、图框标题、刻进楼板的字 | 没有 | 没有「字形轮廓 → 三角网」的路 |
| **图纸文字** | DXF `TEXT`/`MTEXT`、DWFx `Glyphs` | 解析成 `DrawingText`（[drawing_text.h](https://github.com/terry-chao/tamias/blob/main/src/engine/drawing/drawing_text.h)），app 里用 `QPainterPath::addText` 转成轮廓（[drawing_document.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/drawing/drawing_document.cpp)），三维底图整页光栅化成贴图 | 不可拾取 / 不可选 / 不能当几何；Web 壳没有 Qt 那条路 |

### 1.1 缺口清单（都已核对过代码）

| 位置 | 现状 |
|---|---|
| [render_types.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_types.h) `SceneDrawItem` | 只有 mesh / 材质 / 线框，没有文字字段 |
| [render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.h) `FrameSubmission` | 有 `preview_polyline` / `grid_line_segments` / `drawing_overlays`，**没有**文字或字形条目 |
| [render_scene.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene/render_scene.h) | `version = 3`，只有 mesh / item / camera / texture |
| [document_io.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document_io.cpp) | chunk：`META/MESH/SCEN/VIEW/FEAT/MATL/TEXT/RELA/STRY/GRID/DRWG`。注意 **`'TEXT'` 已经被贴图库占了**，文字块得另起 fourcc（建议 `ANNO`） |
| `assets/` | 只有 `branding/`、`icons/`、`samples/`，**没有字体资产** |
| `shaders/` | `mesh / sky / grid / overlay` 四条；没有 `text.*`。WebGL / WebGPU 内嵌着色器里连 `overlay` 都没有（图纸底图是桌面专属） |
| [picking.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.h) | 有 `project_world_to_screen()`（锚点投影现成可用）、Bvh 三角形拾取；屏幕空间文字的命中测试得自己写 |

---

## 2. 文字的数据模型（engine，Qt-free）

落点：`src/engine/render/text/`。**只管「一段文字长什么样、摆在哪」，不碰 Qt、不碰 GPU**（和 `MeshCpu` 的定位一样）。

```cpp
// engine/render/text/text_types.h
enum class TextSpace : std::uint8_t { Screen, World };
enum class TextAlign : std::uint8_t { Left, Center, Right };
enum class TextKind : std::uint8_t {
  Annotation,     // 自由注记
  AxisLabel,      // 轴网编号
  StoreyLabel,    // 标高 / 楼层名
  Dimension,      // 尺寸链数字
  RoomName,       // 房间名
  FrameTitle,     // 图框标题
  DrawingText,    // 从图纸导进来的
};

struct FontRef {
  std::string family;   // "Noto Sans SC" / "" = 默认
  float weight = 400.f; // 400 / 700
  bool italic = false;
};

struct TextStyle {
  FontRef font;
  float size = 14.f;        // Screen：像素高；World：米（字高）
  Vec3 color{0.90f, 0.92f, 0.95f};
  float opacity = 1.f;
  bool pill = false;        // 底下垫一块半透明底板（标注读数常用）
  Vec3 pill_color{0.f, 0.f, 0.f};
  float padding_px = 3.f;
  float letter_spacing = 0.f;
};

struct TextItem {
  std::uint64_t id = 0;      // 拾取 / 脏标记 / 派生去重用；派生标注用 (kind, source_id) 稳定重现
  TextKind kind = TextKind::Annotation;
  TextSpace space = TextSpace::Screen;
  std::string utf8;
  Vec3 anchor_world{};                 // Screen：投到屏幕的锚点；World：文字原点
  Vec3 right{1.f, 0.f, 0.f};           // World：基线方向
  Vec3 up{0.f, 1.f, 0.f};              // World：字面朝向（法线 = right × up）
  TextAlign align = TextAlign::Left;
  TextStyle style{};
  float max_width = 0.f;               // 0 = 不折行
  bool selected = false;
};
```

三条要守住的边界：

1. **屏幕空间标注不进语义树。** 它依赖相机投影，是**视图产物**——和 `grid_line_segments` 一样，每帧在壳里现算（[document_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/viewport/canvas/document_viewport.cpp) `submit_current_frame()`）。否则转一下相机就要 dirty 整个语义树。
2. **模型空间文字进语义树。** 它有身份（可选中、可删、属某楼层），是 `SceneNode` + `mesh_asset_id`，和其他构件一样被楼层 / 类别 / 隔离 / 框选 / 导出管。
3. **渲染侧不存文字内容。** `RenderThread` 只吃「布局好的四边形 + 图集 UV」或「已经烤好的网格」，不认 `std::string`。

---

## 3. 字体与字形资产

### 3.1 字体从哪来

| 场景 | 来源 |
|---|---|
| 桌面 | `assets/fonts/` 入库字体优先；缺字回落到系统字体（`QFontDatabase` 取文件路径，交给 engine 解析） |
| Web / wasm | 只认打包进 `assets/` 的字体（浏览器没有「系统字体文件」这个概念） |
| 缺字 | 按 `FontRef.family` 走回落链：拉丁字体 → CJK 字体 → 默认。找不到的码位画 `□`（tofu）并记一条诊断，不静默丢字 |

**仓库里不放字体二进制**（实现时的取舍）：CJK 全量字体 10 MB 起，而且字体得随附自己的授权文件。改成**运行时查找链**，由壳拼目录，engine 只认目录：

```
assets/fonts（仓库，开发时） → exe 旁边的 assets/fonts（部署） → 系统字体目录 → 一个都没有就整条通路关掉
```

落点：`font_search.h`（`default_font_dirs()` / `list_font_files()` / `pick_default_font()`）+ 壳里的 `app_font_dirs()`。想指定字体就往 `assets/fonts/` 放一份 TTF，不用改代码。P4 做 Web 时得把字体打进包里（浏览器没有「系统字体」这个概念）。

### 3.2 谁解析字体

**vendored `stb_truetype.h`**（public domain，单头）进 `3rdparty/`，和 `rapidobj.hpp` / `vk_mem_alloc.h` 一个风格。理由：engine 不依赖 Qt 是一条硬规矩（见 [ARCHITECTURE.md](ARCHITECTURE.md)），而且 wasm 壳也要同一份布局结果。

`stb_truetype` 给两样东西，正好对应两条渲染通路：

- **度量**（advance / ascent / descent / kerning）→ 布局；
- **字形轮廓**（二次贝塞尔）→ ① 光栅进图集（屏幕空间）② 折线化后三角化（模型空间）。

### 3.3 字形图集（屏幕空间用）

```cpp
// engine/render/text/glyph_atlas.h
struct GlyphKey { std::uint32_t font_id; std::uint32_t codepoint; float px_size; };
struct GlyphSlot { float u0, v0, u1, v1; float bearing_x, bearing_y, advance; };

class GlyphAtlas {
 public:
  // 取字形；没有就现场光栅化并塞进图集（shelf packing）。
  // 图集满是按 LRU 逐出整整一行，并把该行所有 slot 失效（调用方重建布局）。
  Result<GlyphSlot> acquire(const GlyphKey& key);
  [[nodiscard]] const TextureAsset& texture() const;  // RGBA8，白字 + alpha
  [[nodiscard]] std::uint64_t generation() const;     // 变了就重传贴图
};
```

要点：

- 尺寸 1024² 起步，满了涨到 2048²（16 MB）。
- **白字 + alpha，预乘**：现有 overlay 管线的混合就是 `ONE / ONE_MINUS_SRC_ALPHA`（见 [overlay.frag.hlsl](https://github.com/terry-chao/tamias/blob/main/shaders/overlay.frag.hlsl)），文字颜色在片元里乘上去即可。
- CJK 字形多，**只把本帧 + 最近用过的字进图集**；整句长文字（图纸注释）单独光栅成一张小贴图，避免把图集挤爆。
- 上传复用 `RenderThread::upload_texture()`（幂等 + LRU 逐出，见 [render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.h)）。
- P4 起可换 **MSDF**（msdfgen，MIT）拿任意缩放清晰；标注常年 12–16 px，P0 的位图够用。

---

## 4. 渲染方案

### 4.1 屏幕空间标注（billboard）

**新管线**：`shaders/text.vert.hlsl` + `text.frag.hlsl`，`PipelineDesc{ instanced = true, depth_test = false, depth_write = false, blend = true }`。

实例记录（屏幕像素域，不是世界域）：

```cpp
struct TextQuad {
  float rect_px[4];   // x, y, w, h —— 视口像素，左上原点
  float uv[4];        // u0, v0, u1, v1 —— 图集
  float color[4];     // 预乘 rgb + a
};
```

- 顶点着色器：`ndc.x = (rect_px.x + corner.x*w)/fb_w*2-1`，`ndc.y = 1-(rect_px.y + corner.y*h)/fb_h*2`。够简单，不需要矩阵。
- **实现细节（已落地）**：`ndc.y` 的公式**分后端**——Vulkan 裁剪空间 Y 向下、OpenGL 向上，`text.vert.hlsl` 里用 `#if defined(TAMIAS_VULKAN)` 分开写（和 `clip_space_correction_matrix()` 对齐）。视口尺寸借用推送常量里本来没人用的 `eye_pos_mode.xy`，不新增常量块。
- **实例布局直接用 `GpuInstance`**（见 `runtime/text_quad.h` 的 `make_text_instance()`）：四条 RHI 后端只按 `instanced` 标志绑那一套固定属性，字段含义由文字着色器自己解释（`row0` = 像素矩形，`color`，`tex_st` = 图集 UV）。**后端零改动**就是靠这一条。
- 一个 draw 画完所有字：四边形网格是内置单元 quad，其余全靠实例（和 mesh 那条实例化通路一个思路，见 [INSTANCING.md](INSTANCING.md)）。
- **深度策略**：默认不测不写、画在最后（和坐标轴 / 预览线一档），保证标注永远看得见；给「被墙挡住就淡出」的将来需求预留一个 depth-tested 变体。
- **提交接口（已落地）**：`FrameSubmission` 增加
  ```cpp
  std::uint64_t text_atlas_texture_id = 0;
  std::vector<TextQuad> text_quads;
  ```
  壳负责「`TextItem` → 屏幕矩形 + UV」（投影 + 布局调用），engine 负责布局与图集。

**为什么不干脆用 Qt 叠加层？** 桌面 GL 后端下三维区是一个**独立原生子窗口**（`ensure_gl_surface()` 建 `gl_hwnd_`），Qt 控件压不压得住靠 z-order（现在 `coord_label_` / `view_cube_` 靠 `WA_NativeWindow` + `raise()` 顶着）。能立刻用，但：Web 壳没有 QWidget；`.trscn` 快照和测试看不见；混在主窗口里跟三维区裁剪也会打架。所以只当临时停靠，不当架构。

### 4.2 模型空间文字（做进几何）

`stb_truetype` 轮廓 → 折线化 → 三角化 → `MeshCpu` → `Document::intern_mesh()` → `SceneNode` + `TextEntity`。

好处是**白拿**现有全套：Bvh 拾取、框选、楼层 / 类别过滤、`.trscn` 金样、OBJ 导出、将来布尔（把字刻进楼板）、LOD 缓存。

代价：一个字 100–400 三角；改字要重烤网格；远处要 LOD 降级（退化成一个占位块或不画）。

所以**只在「文字必须参与三维遮挡 / 布尔 / 导出」时才走几何路线**。平面图房间名之类用 4.1 的 billboard 更省更清晰。

### 4.3 位置、尺度与投影

- 屏幕锚点 = `project_world_to_screen(view_proj, anchor_world, w, h)`（[picking.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.h) 已有，相机后面返回失败 → 那一条不画）。
- 三种尺度策略，按 `TextKind` 选：**固定像素**（标注默认）、**按世界字高换算像素高**（`px = world_h * fb_h / (2*tan(fovy/2)*dist)`，近大远小）、**恒定屏幕尺寸的世界锚点**（billboard，锚点跟随但不缩放）。
- 模型空间 + 平面视图时，`right/up` 直接取图纸平面基向量，文字躺在标高平面上。

### 4.4 显示策略（「管理」里最容易漏的一块）

| 策略 | 做法 |
|---|---|
| 总开关 | 视图 → Annotations 组：轴号 / 标高 / 尺寸三个勾（`TextKindSet`，按 `TextKind` 分档）✅ |
| 楼层 / 类别过滤 | 模型空间文字在语义树里，自动跟着 `hidden_node_ids` 走；派生标注按来源（轴线 id / 楼层 id）过滤 |
| 平面 / 三维 | 尺寸链只在平面视图显示（`plan_view_`）；轴号、标高两种视图都画 ✅ |
| 视口剔除 | 屏幕矩形与视口相交（CPU，免费） |
| 缩放下限 | 世界空间文字像素高 < N px 不画（防糊成一团） |
| 去重叠 | `LabelOccluder`：屏幕矩形占用表 + padding，**轴号 / 标高 / 尺寸共用一张表**，先到的占住（优先级 = 绘制顺序：轴号 → 标高 → 尺寸）✅ |
| 主题 | 深底 / 浅底反色（图纸已这么干：`DrawingDocument::set_dark_background`） |
| X 光 / 线框 | **不影响文字**（文字不是几何） |

---

## 5. 生命周期与持久化

| | 派生标注（轴号 / 标高 / 尺寸 / 房间名） | 自由文字（注记 / 图框标题） |
|---|---|---|
| 谁产生 | `BimModel` / `Storey` 数据现算 | 用户命令 |
| 存哪 | **不落盘**（视图产物） | `.tdoc` 新增 `ANNO` 块 + `.trscn` 的 `texts` |
| 身份 | `(kind, source_id)` 稳定派生 key | `TextEntity` + `SceneNode`（在语义树里） |
| 撤销 | 不适用（改数据 = 改来源） | 命令：`create_text` / `set_text` / `move_text` / `delete_entity` |
| 三维 / 平面 | 每帧在壳里生成 `TextItem` | 已在 `render_items()` 里（模型空间文字是普通 mesh item） |

具体改动：

- `.tdoc` 加 chunk `'A','N','N','O'`（**别用 `TEXT`，那是贴图库**），写 `TextEntity`：utf8、锚点、`right/up`、样式、所属楼层。读写都按「未知 chunk 跳过」的既有规矩兜底（[document_io.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document_io.cpp)）。
- `.trscn` `version 3 → 4`，新增 `texts`（屏幕空间那一帧的烘焙结果，供金样比对）。digest 纳入文字内容 / 样式 / 锚点；**相机仍不进 digest**（沿用既有规矩，见 [RENDER-SCENE.md](RENDER-SCENE.md)）。
- `EntityKind` 加 `Text = 19`（当前枚举到 `CurtainWall = 18`），同步更新可见性面板的 `entity_kind_catalog` 与图标。
- 命令参数复用文本协议：`name=文字;s:text=会议室;v:pos=1,0,2;i:storey=3`（[command_arg_text.h](https://github.com/terry-chao/tamias/blob/main/src/host/command_arg_text.h) 已支持这几种类型）。
- 插件 / Web：`HostApi` 加 `add_text(utf8, pos, style)`；embind 同步导出（和 Session 能力面对齐，见 [ARCHITECTURE.md](ARCHITECTURE.md) §5）。

---

## 6. 如何引入文字（四条入口，按性价比排序）

### 入口 1：数据派生（最快见效，零 UI 改动）

轴号其实**已经有数据**了——`GridAxis::name` 就是 `"A"` / `"1"`，只是从来没人画。标高在 `Storey::name/elevation`。所以 P1 的第一口就能看到效果：

```
submit_current_frame() 里 append_axis_segments(...) 旁边：
  对每根轴 → 取 axis.name
  → 起点/终点投影到屏幕（project_world_to_screen）
  → 出一圈 TextItem{AxisLabel}
```

这一口不碰文档、不碰命令、不碰持久化，纯视图，风险最低。**✅ 已落地三类**
（`DocumentViewport::append_text_annotations()`）：

| 标注 | 数据来源 | 锚点 | 显示条件 |
|---|---|---|---|
| 轴号 | `GridAxis::name` | 轴线起点（抬到当前楼层标高） | 轴号开关（默认开） |
| 标高 / 楼层名 | `Storey::name` + `format_elevation()` | 模型平面范围左下角外侧 | 标高开关（默认开） |
| 尺寸链 | `grid_dimension_chain()`（相邻轴线间距） | 两轴中间、轴网外缘再外 1.5 m | 尺寸开关 + **平面视图** |

三条都走同一个 `append_label()`：投影锚点 → 挑字体（含中文回落）→ `layout_text()` → 去重叠 →
`append_text_quads()`。整帧字形合并成一次实例化 draw，图集只在 `generation()` 变化时重传。

### 入口 2：交互创建（自由文字）✅ 已落地

Ribbon **Annotate → Text**（`Ctrl+Shift+T`）→ 视口里点一下定锚点 → 弹框输入 → `create_text` 命令（一步撤销）。
放置 / 选中 / 改 / 删都落在 `DocumentViewport`：

| 交互 | 做法 |
|---|---|
| 落位 | `begin_text_placement()` → 下一次左键 `commit_text_placement()`（Esc 取消）；锚点取 `cursor_world_position()`（模型表面 / 工作平面） |
| 选中 | `pick_text_annotation_at()`：屏幕矩形命中，**后放的压在上面**（从后往前找）；选中画成高亮色 |
| 改字 | 双击注记 → `edit_text_annotation()` → `update_text` |
| 删除 | `Delete` / 右键菜单 → `delete_text` |

数据落在 `Document::text_annotations()`（`TextAnnotation`：文本 / 锚点 / 字高 / 颜色 / 对齐 / 类别），
存进 `.tdoc` 的 `ANNO` 块。它**锚在世界点上、朝向屏幕**，所以没有网格、不进 `render_items()`，
拾取是屏幕矩形命中而不是 BVH 三角形命中（理由见 §4.2）。

> 还没做：属性面板里的文字属性页、多行文本编辑、随楼层过滤。

### 入口 3：导入（图纸里的字）

`DrawingText` 已经解析好了，现在只走「QPainterPath 轮廓 / 三维光栅底图」。升级路径：

1. 二维图纸视图：保持现状即可（矢量轮廓已经很清晰）；
2. 三维底图：把 `DrawingText` 转成 `TextItem{DrawingText}` 走文字通路，不再只当位图 —— 好处是清晰（不随图纸光栅分辨率糊）、可搜、将来可选中；
3. 翻模（[DRAWING-TO-BIM.md](DRAWING-TO-BIM.md)）时，房间名 / 楼层名这类可以直接升成模型空间文字。

### 入口 4：API / 脚本

插件 `HostApi`、wasm embind、将来的脚本层。IFC 的 `IfcAnnotation` / `IfcText`（往后接 IfcOpenShell 时）也落这条。

---

## 7. 与现有系统的边界（别越线的地方）

- **语义树只存有身份的文字**（模型空间 / 自由文字）。屏幕空间标注不是文档内容，别让它 dirty `Scene::generation()`。
- **渲染侧不存文本内容**：只吃 `TextQuad` / 网格。
- **`mesh.frag` 不动**，`push constants` 布局不动（文字走自己的管线，或 overlay 的一个 instanced 变体）。
- **`render_items()` 签名不动**：模型空间文字本来就是普通 mesh item；屏幕空间另开 `Document::text_items()`（或由壳直接生成）。
- **`'TEXT'` chunk 是贴图**，文字块用 `ANNO`。

---

## 8. 分阶段落地

| 阶段 | 内容 | 落点 | 验收 |
|---|---|---|---|
| **P0a 布局核** ✅ | 文字类型（`TextItem` / `TextStyle` / `TextKind`…）、UTF-8 解码（脏字节一个不吞）、`GlyphMetricsProvider` 接口、`layout_text()`（折行 / 对齐 / 行高 / 字距） | `src/engine/render/text/*`、`tests/text_layout_tests.cpp` | 16 条单测全绿；engine Qt-free；只依赖 STL |
| **P0b 字体与图集** ✅ | `stb_truetype` 入库；`StbFont`（度量 + 光栅化，支持 .ttf/.otf/.ttc）、`GlyphAtlas`（shelf 装箱 + 扩容 + LRU 逐出 + generation）、字体查找链（**不入库字体二进制**，见 §3.1） | `3rdparty/stb_truetype.h`、`src/engine/render/text/*` | 装箱 / 缓存 / 逐出 / 扩容单测全绿；缺字有 `valid=false` 可诊断 |
| **P1 屏幕空间通路** ✅ | `text.vert/frag.hlsl` + 管线；图集上传；`FrameSubmission::text_quads` + RenderThread 文字 pass；**轴网编号**先跑通 | `shaders/text.*.hlsl`、`src/engine/render/runtime/*`、`src/app/CMakeLists.txt`、`document_viewport.cpp` | 轴号 `A` / `1` 出现在轴线端头；完整链（锚点 → 投影 → 排版 → 图集 → 四边形）有单测。**注：文字还没进 `.trscn`**（见 §8 尾巴） |
| **P2 派生标注** ✅ | 轴号 / 标高 + 楼层名 / 轴网尺寸链；按 kind 开关（Annotations 组）；`LabelOccluder` 去重叠；尺寸链只在平面视图；中文回落字体 | `src/bim/length_text.*`、`src/bim/grid_dimensions.*`、`src/engine/render/text/{label_occluder.*,text_kind_set.h,font_fallback.*}`、`document_viewport.cpp`、`main_window.cpp` | 三类标注都出现、可分别关闭、互不压住；49 条文字/标注单测全绿 |
| **P3 文字注记实体** ✅ | `TextAnnotation`（世界锚点 + 屏幕朝向）+ `Document` 存储 + `create_text` / `update_text` / `delete_text`（都可撤销）+ `.tdoc` 的 `ANNO` 块 + 视口放置 / 屏幕拾取 / 双击改字 / 右键菜单 + Ribbon「Annotate → Text」 | `src/engine/document/text_annotation.h`、`src/command/**/*text*`、`document_io.cpp`、`document_viewport.*`、`main_window.cpp` | 能放 / 能选 / 能改 / 能删 / 能撤销；存盘再打开还在；命令与序列化有单测 |
| **P3b 模型空间几何文字** | 字形轮廓 → 三角网 → `intern_mesh()` → 普通 `SceneDrawItem`（可被遮挡 / 参与布尔 / 导出 OBJ）。带孔洞的字形（`A`、`o`）要么上内核、要么写健壮的多边形三角化 | `src/engine/render/text/outline_tess.*`、`src/command/create/` | 刻字能参与三维遮挡与布尔 |
| **P4 统一与 Web** | 图纸文字并进文字通路；WebGL / WebGPU 文字着色器；MSDF；IFC 文字 | `src/engine/render/rhi/web*/`、`src/app/drawing/` | Web 里有字；放大不糊 |

依赖关系：P0 → P1 → P2 是直的；P3 只依赖 P0（几何那条支路），可以和 P1/P2 并行。

---

## 9. 测试与性能预算

**能自动测的（无窗口、无 GPU）：**

- `TextLayout`：度量、折行、对齐、行距（喂假 `GlyphMetrics`，不依赖字体文件）；
- `GlyphAtlas`：命中复用、LRU 逐出、generation 递增；
- 投影 / 剔除 / 拾取：锚点投影、屏幕矩形相交、屏幕命中；
- 金样：`.trscn` digest 里纳入文字（`RenderSceneText*` 系列，沿用 [RENDER-SCENE.md](RENDER-SCENE.md) 的 Pin 流程）。

**性能预算（先立靶子再实现）：**

| 项 | 目标 |
|---|---|
| 每帧文字四边形 | ≤ 4k（≈ 1 个实例 draw） |
| 图集显存 | 2048² RGBA8 = 16 MB |
| 每帧布局 CPU | < 1 ms（**缓存**：布局只在内容 / 样式 / px_size 变时重算；相机变只重投影锚点，不重排文字） |
| 文字 pass GPU | < 0.2 ms |

---

## 10. 关键文件

| 文件 | 角色 |
|---|---|
| [render_types.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_types.h) | `SceneDrawItem` —— 文字**不**进这里 |
| [render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.h) | `FrameSubmission`：加 `text_quads` / `text_atlas_texture_id` |
| `engine/render/runtime/text_quad.h` | `TextQuad` + `make_text_instance()`（复用 `GpuInstance` 布局） |
| `engine/render/text/glyph_atlas.*` | 字形图集：shelf 装箱 / 扩容 / LRU / 生成代次 |
| `engine/render/text/stb_font.*` | `stb_truetype` 解析：度量 + 光栅化 |
| `engine/render/text/font_search.*` | 字体查找链（`assets/fonts` → 系统目录） |
| `engine/render/text/text_quad_builder.*` | 排版结果 → 屏幕四边形 |
| `engine/render/text/label_occluder.*` | 屏幕矩形占用表（标注去重叠） |
| `engine/render/text/text_kind_set.h` | 按类别的显示开关 |
| `engine/render/text/font_fallback.*` | 按文字内容挑字体（中文回落） |
| `bim/length_text.*` | 标高 / 距离文本（±0.000 / 6.000） |
| `bim/grid_dimensions.*` | 轴网尺寸链（相邻轴线间距） |
| `engine/document/text_annotation.h` | 用户文字注记（世界锚点 + 屏幕朝向） |
| `command/create/create_text_command.*`、`command/edit/update_text_command.*`、`command/delete/delete_text_command.*` | 放 / 改 / 删注记，都可撤销 |
| `document_io.cpp` 的 `kChunkAnno` | `.tdoc` 的 `ANNO` 块 |
| `shaders/text.vert.hlsl` / `text.frag.hlsl` | 屏幕空间文字管线（实例化、预乘混合） |
| [picking.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.h) | `project_world_to_screen()`：锚点投影现成可用 |
| [drawing_text.h](https://github.com/terry-chao/tamias/blob/main/src/engine/drawing/drawing_text.h) | 图纸文字（导入入口） |
| [grid_axis.h](https://github.com/terry-chao/tamias/blob/main/src/bim/grid_axis.h) | 轴号来源（`GridAxis::name`） |
| [document_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/viewport/canvas/document_viewport.cpp) | `submit_current_frame()`：屏幕空间标注在这里生成 |
| [document_io.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document_io.cpp) | `.tdoc` 块读写（加 `ANNO`） |
| [render_scene.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene/render_scene.h) | `.trscn` 版本与内容（v4 加 `texts`） |
| [INSTANCING.md](INSTANCING.md) | 实例化通路（文字四边形沿用同一思路） |
