# 第 8 章　渲染管线：一帧是怎么画出来的

> 本章目标（★ 骨架章节）：看懂从 CPU 三角网到屏幕像素的整条链路——谁提交、谁画、RHI 为什么存在、NDC 是什么。

## 8.1 先建立直觉

显示器不会画「盒子」「墙」「BRep」。它只会给每个像素一个颜色。整套渲染只干一件事：

> **把三维里的三角积木，经过相机投影，涂上颜色，写进窗口的每一帧。**

## 8.2 五段分工（核心表）

| 谁 | 干什么 | 不懂渲染时可以想成 |
|---|---|---|
| 特征树 / 导入 | 做出三角网（CPU 上的积木） | 做道具 |
| 语义树 `Scene` | 记住谁在哪、什么颜色、选中没 | 演员表和站位 |
| 视口 `DocumentViewport` | 每帧打包「这一镜要画什么」 | 导演喊 action |
| 渲染线程 `RenderThread` | 真去 GPU 上画 | 摄影棚 |
| RHI（Vulkan / OpenGL） | 把「画」翻译成两种 GPU 方言 | 两套摄影机说明书 |

中间那张快递单叫 **`SceneDrawItem`**（[render_types.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_types.h)）：网格 id + 世界矩阵 + 材质 + 选中标志。语义侧填好，渲染侧照着画。

## 8.3 三角网：GPU 唯一认得的形状

```cpp
顶点 Vertex：位置 + 法线 + UV + 颜色
索引 indices：每三个下标组成一个三角形
```

CPU 里叫 `MeshCpu`，上传到显存叫 `GpuMesh`（VBO + IBO）。上传后**留在显存**（半留存），场景结构（谁在哪）每帧重新交一份清单。

## 8.4 一帧怎么走

```
Scene（语义树）
  → Document::render_items() 展平（世界矩阵已算好；屏外叶子已剔除）
    → SceneDrawItem 列表
      → 视口填 FrameSubmission（窗口、相机、清单、显示模式）
        → RenderThread.draw_channel
          → 顺序画：清屏→天空→地面网格→每个 item 一次 draw→坐标轴→预览线
            → RHI：Vulkan 或 OpenGL
              → 窗口像素
```

`draw_channel` 是整套渲染的心脏（[render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.cpp)）。**绘制代码不认 Vulkan / OpenGL**——它只认 RHI 的动词：`create_buffer` / `set_texture` / `draw_indexed`。

## 8.5 RHI：为什么要自己写一层

GPU 厂商给的是两套完全不同的 C API。Tamias 自研一层 RHI（[device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h)）：

```
src/engine/render/rhi/
  device.h          抽象：建缓冲/纹理/管线、draw_indexed、present
  vulkan/           主后端
  opengl/           副后端（兼容老卡）
  webgl/            浏览器后端
  wgpu/             方案（代码未落地，第 10 章）
```

可以想成：导演只说「画这个网格」，翻译官分别说俄语（Vulkan）和英语（OpenGL）。两套差异被藏在 `clip_space_correction_matrix()`（NDC 约定不同）和各自的 shader 变体里。

## 8.6 NDC：视锥是金字塔，NDC 是立方体

每个顶点走：

```
物体空间 --M--> 世界 --V--> 相机空间 --P--> 裁剪空间 (x,y,z,w)
                                            │ 除以 w（透视除法）
                                            ▼
                                    NDC 立方体 (x,y,z)
                                            │
                          ┌─────────────────┴─────────────┐
                          ▼                               ▼
                  xy → 视口 → 屏幕像素            z → 深度缓冲
```

新手最容易混的一句话：**NDC 不是一张虚拟平面，是立方体**。xy 上屏，z 进深度缓冲决定谁挡住谁。Vulkan 的 z 是 [0,1]、OpenGL 是 [-1,1]——这就是 clip 校正矩阵存在的原因。详见 [视锥、NDC 与屏幕](../NDC.md)。

## 8.7 显示模式与材质

三种模式在 shader 里用 `mode` 区分（[mesh.frag.hlsl](https://github.com/terry-chao/tamias/blob/main/shaders/mesh.frag.hlsl)）：

| 模式 | 像素上做什么 |
|---|---|
| 线框 Wireframe | 深灰线，不打光；选中变橙 |
| 着色 Shaded | Lambert：颜色 × 光线点积 |
| 真实 Realistic | 简化 PBR（GGX + 半球环境） |

材质从文档走到像素：`Material`（颜色/粗糙度/金属度/贴图）→ `render_items()` 写进 item → 视口上传纹理 → draw 时 set_texture + push constants → fragment shader 定最终颜色。贴图用 triplanar（世界坐标投影采样），不用网格 UV。

## 8.8 现在刻意没做的

合批 / instancing、透明排序、阴影、AO、渲染侧场景图——这些是路线图的性能命门，不是漏画。

## 8.9 动手练习

1. 在 `RenderThread::draw_channel()` 断点，数一数一帧里发了多少次 `draw_indexed`。
2. 把相机转到墙背后，对比「帧里有没有这条墙的 item」（视锥剔除的作用，见 [视锥剔除](../FRUSTUM-CULLING.md)）。
3. 在设置里切 Vulkan ↔ OpenGL，确认画面一致——这就是 RHI 抽象存在的意义。

## 延伸阅读

- [管线与 RHI](../RENDERING.md)：完整版（含 shader、材质、总图）
- [视锥、NDC 与屏幕](../NDC.md)：坐标变换深讲
- [OpenGL 后端](../OPENGL.md)：绑缓冲、画三角、贴图的具体代码
- [视锥剔除](../FRUSTUM-CULLING.md)：屏外不发 draw

下一章：[BIM 业务层](09-bim-layer.md)
