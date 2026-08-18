# OpenGL 后端在 Tamias 里干什么

> OpenGL **不是场景图，也不自己找三角面**。它是 RHI 的一块翻译官：渲染线程已经决定画哪份网格、用哪张贴图，OpenGL 只负责绑缓冲、发 `glDrawElements`、换缓冲。一帧剧本见 [管线与 RHI](RENDERING.md)；屏外不画见 [视锥剔除](FRUSTUM-CULLING.md)。

实现：[opengl_device.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/opengl/opengl_device.cpp)。抽象接口：[device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h)。

---

## 0. 它在整条链的哪一层

```
OCCT / OBJ  → MeshCpu（CPU 上的顶点+索引）
Document     → SceneDrawItem（谁、世界矩阵、材质 id）
视口         → FrameSubmission 交给 RenderThread
RenderThread → 只认 RHI 动词：create_buffer / set_texture / draw_indexed
OpenGL 后端  → 把这些动词变成 glBind* / glDrawElements / SwapBuffers
```

设置里选 OpenGL 后，每个文档**独占一条渲染线程、一个 GL 上下文**，不和别的视口、也不和 Vulkan 共用（`shares_execution_thread_with` 对 OpenGL 永远为假）。Vulkan 可以多视口共线程；OpenGL 的上下文绑定是线程局部的，所以必须隔离。

启动：`main()` → [`register_opengl_backend()`](https://github.com/terry-chao/tamias/blob/main/src/app/rhi_backends.cpp) 登记工厂。视口 `acquire(OpenGL)` 才真正 `wglCreateContextAttribsARB` 做出 **OpenGL 4.5 Core**。

---

## 1. 画在哪：窗口怎么绑

Windows 上 Qt 控件本身不当 GL 表面。UI 线程在视口里再开一个**子 HWND**（`create_opengl_surface_hwnd`），像素格式带深度、双缓冲。这个 HWND 设成 `HTTRANSPARENT`，鼠标仍给下面的 Qt 视口（旋转、点选）。

每帧 `DocumentViewport::native_handle()` 交出这个 HWND。渲染线程 `begin_frame`：`GetDC` + `wglMakeCurrent`，画完 `SwapBuffers`。资源创建（缓冲、贴图、shader）走一个隐藏 dummy 窗口上的**同一上下文**。

Linux 是 GLX + X11 窗口，逻辑一样。

---

## 2. 三角面从哪来（OpenGL 不找面）

三角在进 GPU **之前**就定好了：

| 来源 | 谁生成 `MeshCpu` |
|---|---|
| 盒子/墙 | 特征树 → OCCT tessellate（[造型](FEATURE-TREE-EVALUATOR.md)） |
| OBJ/GLB | mesh loader |
| STEP | `IShapeOps` tessellate（[几何边界](ISHAPE-OPS.md)） |

`MeshCpu` = `vertices[]`（位置 / 法线 / UV / 色）+ `indices[]`（每三个下标一个三角）。结构见 [mesh.h](https://github.com/terry-chao/tamias/blob/main/src/engine/graphics/mesh.h)。

视口 `upload_mesh` 把这份 CPU 数据丢进渲染线程。[`RenderThread::upload_mesh`](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.cpp)：

1. `glGenBuffers` 建 VBO，`glBufferSubData` 写入顶点
2. 再建 IBO，写入 `uint32` 索引
3. 记成 `GpuMesh`：两个 Buffer + `index_count`
4. 表 `asset_to_gpu_[mesh_asset_id] → gpu_id`

**OpenGL 从不遍历 Scene、也不读 BRep。** 它只看到已经上传的 VBO/IBO。点选是 CPU 上 BVH 打三角，和 GL 无关。

---

## 3. 一帧怎么画（绑定 → 绘制）

`draw_channel` 对 OpenGL 和 Vulkan 是**同一套 C++**。OpenGL 的 `CommandList::execute` 是空的：每句 `set_*` / `draw_indexed` **当场变成 GL 调用**（不是先录命令再提交）。

顺序：

1. `make_current` 绑到子 HWND
2. `glClear` 色 + 深度
3. 天空 → 地面网格 → **模型** → 轴 → 预览线
4. `SwapBuffers`

画一个模型时：

```
mesh_asset_id → GpuMesh（VBO+IBO）
albedo_texture_id → GpuTexture，没有就 1×1 白图
set_pipeline（着色/线框 program）
set_texture(slot 0)
set_push_constants（MVP、颜色、粗糙度、has_albedo、眼睛…）→ 其实是 UBO binding 0
set_vertex_buffer / set_index_buffer   ← 只记指针
draw_indexed(index_count)
```

`OpenGLCommandList::draw_indexed` 里才真正绑：

- 共用一个 VAO
- `glBindBuffer(GL_ARRAY_BUFFER, VBO)`
- 属性 0/1/2/3 = 位置 / 法线 / UV / 色（按 `Vertex` 布局）
- `glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO)`
- `glDrawElements(GL_TRIANGLES, index_count, UNSIGNED_INT, …)`
  线框管线是 `GL_LINE` 的 polygon mode，拓扑仍是三角

**「找到三角面」= 索引缓冲里事先排好的下标。** GPU 按每三个索引取顶点光栅化。CPU 不在 draw 时再搜面。

Push constants 在 GL 侧是一块 UBO：`glBufferSubData` + `glBindBufferBase(0)`。Shader 仍当 push constant 用，两边布局对齐。

Clip：CPU 的 `perspective` 按 Vulkan 的 Z∈[0,1] 算。OpenGL 要 NDC Z∈[-1,1]，所以乘 `clip_space_correction_matrix()`（把 Z 从 [0,1] 拉成 [-1,1]）。Vulkan 那份校正是翻 Y；两套校正不同，世界空间视锥是一样的。NDC 是立方体、xy 上屏、z 进深度，见 [视锥、NDC 与屏幕](NDC.md)。

---

## 4. 纹理怎么绑

上传（渲染线程，dummy 上下文）：

- `glGenTextures` → `glTexImage2D` 整张 RGBA8
- albedo 用 `GL_SRGB8_ALPHA8`（采样时硬件转线性）
- `GL_LINEAR` + `GL_REPEAT`

画的时候：

```
glActiveTexture(GL_TEXTURE0 + slot)   // 现在永远 slot 0
glBindTexture(GL_TEXTURE_2D, handle)
```

`SceneDrawItem.albedo_texture_id` 有、且已 upload → 绑那张；否则绑默认白纹理。`has_albedo` 写进 UBO：0 则 fragment 用纯 `base_color`。采样是 **triplanar**（世界坐标投到三个平面），不用网格 UV。法线贴图 id 传到常量里了，shader **还没采样**。

网格 / 轴线即使不采样贴图也会绑白纹理，避免采样器空绑（Vulkan 更硬，GL 这边同样绑上以求两边路径一致）。

Shader：HLSL → SPIR-V。OpenGL 加载 `*.gl.spv`，`glShaderBinary` + `glSpecializeShader`（要 `GL_ARB_gl_spirv`），链成 program。不是运行时编 GLSL。

---

## 5. 一句话对照

| 问题 | 答案 |
|---|---|
| OpenGL 是什么 | RHI 后端：4.5 Core，立即模式命令 |
| 谁给它三角 | `MeshCpu` 上传成 VBO/IBO；清单用 `mesh_asset_id` 查找 |
| 怎么绑 | 每 draw 绑 VAO + VBO + IBO + program + 纹理0 + UBO |
| 怎么画 | `glDrawElements`，索引=三角（或线框的三角边） |
| 纹理谁给 | 文档 `TextureAsset` → upload → `albedo_texture_id` 对上再 `glBindTexture` |
| 它不管什么 | 特征树、OCCT、点选、视锥剔除、合批 |

换 Vulkan 时，上面 `draw_channel` 一行都不用改；变的是 `opengl_device.cpp` 里那些 `gl*` 换成 `vkCmd*`。OpenGL 在项目里就是「同一套绘制剧本的 GL 方言」。
