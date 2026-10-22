# WebGL 转默认 / WebGPU 转实验 —— 实施清单

> 状态：**主体已落盘**（2026-10）。WebGL 已是 `wasm` 预设的默认后端，WebGPU 降为
> `wasm-webgpu` 实验预设；产物已核对：`tamias_viewer.js` 里 `GLctx` 97 命中、
> `wgpu` **0** 命中（切换前正好相反：GL 全 0、wgpu 165）。
> 剩余缺口见下面「落地情况」表的 ❌ / 🟡——**先看那张表再当作承诺**。
>
> 目标见会话 goal；范围经用户确认：WebGL 为默认、WebGPU 改为可选**实验**预设。

## 落地情况（实测，非推断）

| 节 | 内容 | 状态 |
|---|---|---|
| §1 | `CMakePresets.json`：`wasm` → WebGL 默认；新增 `wasm-webgpu` + `wasm-webgpu-build` | ✅ |
| §1b | **清单里漏掉的一个前提**：`CMakeLists.txt:91-99` 原来把两个 web 后端 `set(... FORCE)` 死了，预设的选择**根本生效不了**。改成 EMSCRIPTEN 感知的 `option()` 默认值 + 去掉那两条 FORCE，并加了一条"一个后端都没选"的 FATAL_ERROR | ✅ |
| §1c | **清单里漏掉的第二个前提**：Emscripten 的 `MIN/MAX_WEBGL_VERSION` 缺省都是 **1**，而 `rhi/webgl` 用 WebGL2 的 UBO 一族；不抬到 2 就在**链接期**报 `glBindBufferBase` / `glGetUniformBlockIndex` / `glUniformBlockBinding` undefined。已在 `src/wasm/CMakeLists.txt` 按 `TARGET tamias::rhi_webgl` 条件加上 | ✅ |
| §2.1 | `viewer_host.cpp` 后端选择改为 **WebGL 优先**，WebGPU 分支补实验告警 | ✅ |
| §2.2 | `backend_name()` / `backend_experimental()` + embind 导出 | ✅ |
| §2.3 | 前端顶栏后端读数 + 橙色「实验」角标 | ✅ **但机制变了**：顶栏后来按桌面端重做过，原来的 `--exp` CSS 变量没了；现在是顶栏角落的 backend chip（`.tb-chip.exp`），数据仍来自 `backendExperimental()` |
| §3 | text / overlay 两条 GLSL 管线 + `render_runtime.cpp` 接线 | 🟡 **WebGL 着色器与管线已补（2026-10-22）**：`text` / `overlay` / 屏幕空间 `line` 三对 GLSL ES 3.00 已进 `webgl_shaders.h`，`render_runtime.cpp` 改为按后端分派（WebGL GLSL / 桌面 SPIR-V / WebGPU 暂缺 WGSL）。**但浏览器里文字与底图仍不出现**——`ViewerHost::render()` 不提交 `text_quads` / `drawing_overlays`（详见 `docs/WEBGL-PIPELINE-AUDIT.md` §2① 的「第二层」）。立刻生效的只有坐标轴改走屏幕空间像素线 |
| §4.1 | `frameTimeMs()`（帧耗时 / FPS 读数） | ❌ 未做（embind 里确认没有这个导出） |
| §4.2 | 日志面板可折叠 / 复制 / 清空 | 🟡 **折叠已做**：文件菜单 / 视图段有「引擎日志」开关（`log-panel`，面板有内容才画）。复制、清空仍未做 |
| §5 | 交互（X 光开关、正交切换、框选、夹点编辑） | ❌ 未做（四项都还在）。**但正交那项比原来记的更小**：`camera.h:147` 已有 `set_orthographic()` / `orthographic()`，只差 embind 导出 + 按钮，见下面 §5 的表 |
| §6 | 文档同步：`docs/WEB.md`、`docs/WGPU.md`、`docs/ROADMAP.md` | ✅（`docs/WEB.md` 后来又随造型内核 / 文档生命周期 / 文件管理页几轮重写过） |
| §7 | 验证：产物含 GL 胶水；`tamias_server` 与几何服务的 F5 链路另见 `docs/DECISION-SERVER-LANGUAGE.md` | ✅ |

> **后续轮次已经动过这份清单里的东西**：顶栏从"三个显示模式按钮"扩成 13 组 43 个工具
> （图标改用桌面端同一份 SVG），并加了文档生命周期（新建 / 打开 / 关闭 / 文件管理页）。
> 所以上表里凡是提到"顶栏"的落点，都要按 `docs/WEB.md` §4 的现状读，别照着本文件的旧描述找。

## 附带产出：F5 调试链路

`scripts/wasm-preview.ps1 -WithGeometryServer` + `scripts/serve-geometry.cmd` +
`.vscode/{launch,tasks}.json`：调试 WebGL 查看器时**一律**自动起 .NET 几何服务
（:5199）+ 静态预览（:3000）+ 打开浏览器（VS Code 里只有这一个浏览器配置；
不要几何服务时走脚本，不带 `-WithGeometryServer`）。这条链路与 §1–§2 的切换是配套的。

---

## 0. 现状事实（已实测，非推断）

| 事实 | 证据 |
|---|---|
| `rhi/webgl` 从未在 wasm 预设下编过 | `build/wasm/bin/tamias_viewer.js` 与 `.wasm` 中 `glDrawElements` / `GLctx` / `emscripten_webgl_create_context` / `WebGL2 RHI` **零命中** |
| 原因 | `CMakePresets.json:65` → `TAMIAS_ENABLE_WEBGL_BACKEND: "OFF"`，`wasm` 预设是 WebGPU-only |
| 两条管线对 web 后端直接跳过 | `render_runtime.cpp:812`、`:862` 均为 `if (!webgl && !webgpu)` |
| WebGL 后端本身是完整的 | `webgl_device.cpp` 635 行，Buffer/Texture/Shader/Pipeline/Fence/SwapChain/CommandList 齐全 |
| 两后端 shader 覆盖面一致 | `webgl_shaders.h` 与 `webgpu_shaders.h` 都只有 `mesh_vert/frag` + `sky_vert/frag`，**都缺** text / overlay |

**结论**：WebGL 的缺口不是「接入」，而是「选为默认 + 补齐两条管线 + 补齐调试交互」。

---

## 1. CMakePresets.json 改动

当前 `wasm` 预设（第 44–77 行）：`displayName: "Emscripten WebGPU viewer"`，
`TAMIAS_ENABLE_WEBGL_BACKEND: "OFF"` / `TAMIAS_ENABLE_WEBGPU_BACKEND: "ON"`。

改为：

```jsonc
{
  "name": "wasm",
  "displayName": "Emscripten WebGL viewer",   // 原：WebGPU viewer
  "binaryDir": "${sourceDir}/build/wasm",
  // ... 其余 environment 不变 ...
  "cacheVariables": {
    // ...
    "TAMIAS_ENABLE_WEBGL_BACKEND": "ON",       // 原 OFF —— 默认后端
    "TAMIAS_ENABLE_WEBGPU_BACKEND": "OFF",     // 原 ON  —— 转为实验，移出默认
    // ... 其余不变 ...
  }
}
```

新增实验预设（同名继承，仅覆盖后端开关与 binaryDir）：

```jsonc
{
  "name": "wasm-webgpu",
  "displayName": "Emscripten WebGPU viewer (实验)",
  "inherits": "wasm",
  "binaryDir": "${sourceDir}/build/wasm-webgpu",
  "cacheVariables": {
    "TAMIAS_ENABLE_WEBGL_BACKEND": "OFF",
    "TAMIAS_ENABLE_WEBGPU_BACKEND": "ON"
  }
}
```

`buildPresets` 里 `wasm-build` 目前 `configurePreset: "wasm"`（第 131 行）无需改动，
另加一条 `wasm-webgpu-build` 指向新预设。

**注意**：`TAMIAS_ENABLE_WEBGL_BACKEND: ON` 会改变 `rhi/CMakeLists.txt:22` 的分支，
确认 `tamias_rhi_webgl` 目标进入 `tamias_viewer` 的链接（`src/wasm/CMakeLists.txt` 已有
`if(TARGET tamias::rhi_webgl)` 分支，无需改）。

---

## 2. 后端选择与「实验」运行时标注

### 2.1 viewer_host.cpp

`start()`（`src/wasm/viewer_host.cpp:157`）现为编译期 `#if WEBGPU / #elif WEBGL`。
改为 **WebGL 优先**：

```cpp
#if defined(TAMIAS_HAS_RHI_WEBGL)
  register_webgl_backend();
  config.backend = GraphicsBackend::WebGL;
#elif defined(TAMIAS_HAS_RHI_WEBGPU)
  register_webgpu_backend();
  config.backend = GraphicsBackend::WebGPU;
  log_warn("WebGPU 后端为实验特性，功能与稳定性尚未与 WebGL 对齐");
#else
  return Err("no WASM RHI backend was compiled");
#endif
```

### 2.2 新增 embind 导出（后端名 + 实验标记）

`ViewerHost` 加两个只读 getter，`viewer_main.cpp` 的 `EMSCRIPTEN_BINDINGS` 里导出：

```cpp
std::string backend_name() const;   // "WebGL2" / "WebGPU"
bool backend_experimental() const;  // WebGPU → true
```

`webgl_device.cpp` 已 `log_info("WebGL2 RHI initialized")`；WebGPU 侧需补一条
带「实验」字样的 `log_warn`，使其进入 `logText()`，页面日志面板自动可见。

### 2.3 web 前端角标

`web/src/viewer.ts` 的 `TamiasViewerModule` 接口加：

```ts
backendName(): string;
backendExperimental(): boolean;
```

`web/src/App.tsx` 顶栏 `brand` 里（第 386–392 行的「施工中」旁边）加：

```tsx
{backendExp && (
  <span className="wip" title="WebGPU 后端为实验特性，尚未与 WebGL 对齐">
    {backendName}（实验）
  </span>
)}
```

`web/src/styles.css` 复用现有 `.wip` 样式，或加一个橙色变体 `.wip.exp`。

---

## 3. 补齐两条 GLSL 管线（text / overlay）

> **状态（2026-10-22）**：WebGL 侧**已落地**，并且多补了一条屏幕空间像素线
> （`line`，原来只有桌面 SPIR-V）。三条管线的建法抽成
> `render_runtime.cpp` 里的 `create_optional_pipeline()`；WebGPU 仍缺 WGSL（实验预设）。
> 注意本节只解决「管线不存在」，**不解决「没有数据」**：`ViewerHost` 至今不填
> `text_quads` / `drawing_overlays` / `preview_*` / `grip_points` / `grid_*`，
> 所以浏览器画面上的文字与底图仍然不可见。第二层见
> [WebGL 链路审计](docs/WEBGL-PIPELINE-AUDIT.md) §2①。

### 3.1 关键约束：`kPushBlock` 必须逐字复用

`webgl_shaders.h:8` 的 `kPushBlock` 是 `std140` 的 **208 字节** UBO 数据（`PushConstants`
共 7 槽 × 16 B），放在 256 B 的 GL buffer 里；对应 `shaders/mesh.hlsli:4` 的
`PushConstants`。text / overlay / line 都**必须拼接同一份 `kPushBlock`**，否则 UBO
偏移错位。

text 用到的字段：`pc.eye_pos_mode.xy` = **视口像素尺寸**（复用约定，见
`shaders/text.vert.hlsl:9` 注释）；overlay 用 `pc.mvp` 与 `pc.color.a`。

### 3.2 顶点属性位置

`WebGLCommandList::draw_indexed`（`webgl_device.cpp:284`）固定绑定 location 0–3
（position/normal/uv/color）与实例 4–9（GpuInstance 六行）。text / overlay 走同一
VAO 逻辑，因此：

| 管线 | 用到的 location | 来源 |
|---|---|---|
| text | 0 (position)、2 (uv)、实例 4 (rect)、7 (color)、9 (tex_st) | 与 `text.vert.hlsl:11-15` 一致 |
| overlay | 0 (position)、2 (uv)、3 (color) | 与 `overlay.vert.hlsl:9-11` 一致 |

### 3.3 待加入 `webgl_shaders.h` 的四个函数

签名与现有风格一致（`inline std::string_view xxx()` 返回 `static const std::string`）。
GLSL ES 3.00，注意 **Y 轴要翻转**（`text.vert.hlsl:34` 的 GL 分支），
而 `clip_space_correction_matrix()`（`webgl_device.cpp:209`）只做 Z 映射、不动 Y。

**`text_vert()`** —— 由 `shaders/text.vert.hlsl` 直译：

```glsl
#version 300 es
precision highp float;
/* kPushBlock 拼接在这里 */
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;
layout(location = 4) in vec4 a_rect;    // xy 左上角(px)  zw 尺寸(px)
layout(location = 7) in vec4 a_color;
layout(location = 9) in vec4 a_tex_st;  // xy 缩放  zw 偏移
out vec2 v_uv;
out vec4 v_color;
void main() {
  vec2 viewport = max(pc.eye_pos_mode.xy, vec2(1.0));
  vec2 pixels = a_rect.xy + a_position.xy * a_rect.zw;
  float ndc_x = pixels.x / viewport.x * 2.0 - 1.0;
  float ndc_y = 1.0 - pixels.y / viewport.y * 2.0;   // GL 分支：像素 y 向下，翻
  gl_Position = vec4(ndc_x, ndc_y, 0.0, 1.0);
  v_uv = a_tex_st.zw + a_uv * a_tex_st.xy;
  v_color = a_color;
}
```

**`text_frag()`** —— 直译 `shaders/text.frag.hlsl`（预乘 alpha）：

```glsl
#version 300 es
precision highp float;
/* kPushBlock */
uniform sampler2D atlas_tex;
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
void main() {
  float coverage = texture(atlas_tex, v_uv).a;
  float alpha = clamp(coverage * v_color.a, 0.0, 1.0);
  frag_color = vec4(v_color.rgb * alpha, alpha);
}
```

**`overlay_vert()`** —— 直译 `shaders/overlay.vert.hlsl`：

```glsl
#version 300 es
precision highp float;
/* kPushBlock */
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_color;
out vec2 v_uv;
out vec3 v_color;
out float v_opacity;
void main() {
  gl_Position = pc.mvp * vec4(a_position, 1.0);
  v_uv = a_uv;
  v_color = a_color;
  v_opacity = pc.color.a;
}
```

**`overlay_frag()`** —— 直译 `shaders/overlay.frag.hlsl`：

```glsl
#version 300 es
precision highp float;
/* kPushBlock */
uniform sampler2D albedo_tex;
in vec2 v_uv;
in vec3 v_color;
in float v_opacity;
out vec4 frag_color;
void main() {
  vec4 tex = texture(albedo_tex, v_uv);
  float alpha = clamp(tex.a * v_opacity, 0.0, 1.0);
  frag_color = vec4(tex.rgb * v_color * alpha, alpha);
}
```

### 3.4 render_runtime.cpp 接线

把 `:812` 与 `:862` 的第一个判断由 `if (!webgl && !webgpu)` 改为按后端分派：
WebGL 走 `webgl_shaders::text_vert()` 等内嵌 GLSL（参考 `:652` 的 mesh 分支写法），
其余后端保持现有 SPIR-V 路径。

**管道参数照抄现有值，不要改**：

| 管线 | instanced | depth_test | depth_write | blend |
|---|---|---|---|---|
| text（`:842-849`） | `true` | `false` | `false` | `true` |
| overlay（`:893-901`） | `false` | `true` | `false` | `true` |

混合在 `webgl_device.cpp:270` 已是 `ONE / ONE_MINUS_SRC_ALPHA`（预乘），与两个
shader 的输出约定一致 —— **这条已经对了，不用动**。

WebGPU 侧的 WGSL 版本（`webgpu_shaders.h` 加同样四条）属同一 pull request，
但优先级低于 WebGL（WebGPU 已降为实验）。

---

## 4. 调试完善

### 4.1 引擎侧新增导出

| 导出 | 来源 | 用途 |
|---|---|---|
| `backendName()` | `device->backend()` → `rhi_selection/backend.h:24` 的 `to_string` | 读数显示 |
| `backendExperimental()` | 常量判断 | 实验角标 |
| `frameTimeMs()` | 见下 | 帧耗时 / FPS |

帧耗时建议在 `ViewerHost::render()`（`viewer_host.cpp:397`）里用
`std::chrono::steady_clock` 掐 `channel_->submit()` + `render_thread_->pump()`
两段，保留最近 N 帧的环形缓冲并算平均值。**不要**用 `glFinish()` 计时，
那会串行化管线、把数字变得没意义。

### 4.2 前端

- `stats` 读数条（`App.tsx:522`）加 `gpu` 旁边的 **`backend`** 与 **`fps`** 字段。
  注意 WebGL 用 `requestAnimationFrame`，FPS 应取 400ms 轮询间隔内的帧数，
  而不是瞬时值。
- 日志面板（`App.tsx:530-535`）现已存在但**不可折叠、不可复制**。改为：
  - 标题行加「复制」「清空」「折叠」三个按钮；
  - 折叠状态用 `useState` 记住，折叠时只显示角标计数；
  - 「复制」走 `navigator.clipboard.writeText(logText)`，失败回退到选中文本。

---

## 5. 用户交互完善

按工作量从小到大排（建议同轮只做前两项）：

| 项 | 落点 | 说明 |
|---|---|---|
| **X 光开关** | `ViewerHost` 已有 `xray_` 成员与 `xray()` 读取（`viewer_host.h:56` / `:123`，`.trscn` 加载时会还原），`viewer_main.cpp` 加 `setXray(bool)` 导出，工具带加按钮 | 最小，引擎侧逻辑已存在，只缺 setter + 导出 |
| **正交 / 透视切换** | 相机**已经有** `set_orthographic()` / `orthographic()`（`src/engine/math/camera.h:147-148`，投影分支在 `:118`）——本条原来把路径写成了 `src/engine/render/camera_controller.h`（不存在），实际在 `src/engine/math/camera.h`。加 embind 导出 + 按钮即可 | 小 |
| **框选** | 需要在 `ViewerHost` 里做屏幕矩形 → 与 BVH 求交（复用 `pick_entity` 的 `camera_ray`，`viewer_host.cpp:302-319`），或者退化为「包围盒投影到屏幕后测试」 | 中 |
| **夹点编辑** | 在 `docs/WEB.md` 的「对照桌面端」表里列为 ❌（别按行号找，那张表被重写过）；需引擎侧新增夹点拾取 + 拖拽提交为可撤销命令 | 大，建议独立一轮 |

---

## 6. 文档同步

| 文件 | 改法 |
|---|---|
| `docs/WGPU.md` | 标题下加实验状态横幅；`:3` 的「浏览器后端已落地」改为「实验」；补「不保证与 WebGL 对齐」 |
| `docs/WEB.md` | `:5` 的「图形默认 WebGPU」改为「默认 WebGL2，WebGPU 为实验可选」；`:22` 表格同步；`:96` 与 `:156-157` 的开关默认值同步；`:156` 表格里 WebGL 由「可选回退」改为「默认」 |
| `CMakePresets.json` | 见第 1 节 |
| `README.md` | `:15` 「切换浏览器（WASM）预设见 docs/WEB.md」无需改；如提及 WebGPU 则同步 |
| `docs/ROADMAP.md` | 若列了 WebGPU 默认，需降级措辞 |

---

## 7. 验证步骤（权限恢复后）

1. **编通**：用独立构建目录，避免动现有 WebGPU 产物
   ```
   emcmake cmake -S . -B <可写目录> -G Ninja -DCMAKE_TOOLCHAIN_FILE=<emscripten> \
     -DTAMIAS_ENABLE_WEBGL_BACKEND=ON -DTAMIAS_ENABLE_WEBGPU_BACKEND=OFF \
     -DTAMIAS_ENABLE_VULKAN_BACKEND=OFF -DTAMIAS_ENABLE_OPENGL_BACKEND=OFF \
     -DTAMIAS_ENABLE_OCCT=OFF -DTAMIAS_BUILD_TESTS=OFF -DVCPKG_MANIFEST_MODE=OFF
   cmake --build <可写目录> --target tamias_viewer
   ```
2. **产物含 GL 胶水**：在 `tamias_viewer.js` / `.wasm` 字符串表里确认
   `GLctx`、`_glDrawElements`、`emscripten_webgl_create_context` 命中数 **> 0**
   （当前是 0 —— 这是「编进去了」最硬的证据）。
3. **功能**：`scripts/wasm-preview.ps1` 起预览，浏览器打开后确认
   - 立方体 / `.obj` / `.tdoc` 能转、能点选；
   - 新的 `backendName()` 返回 `WebGL2`；
   - 文字标注与图纸底图管线建立成功（`logText()` 里无
     `text shaders unavailable` / `drawing overlay shaders unavailable`）。
4. **回归**：`cmake --build build/wasm --target tamias_viewer`（WebGPU 实验预设）
   仍能编通，且 WebGPU 路径下页面显示「（实验）」角标。
