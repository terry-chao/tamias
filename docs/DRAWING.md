# 参考图纸（2D 图纸页）

把已有的图纸文件当参考底图看：**PDF / DXF / SVG / 位图**。它和三维视口是两类页签，
同一份工程里可以一边看模型、一边对照图纸。

> 这是**路线 A**：显示已有图纸。从三维模型投影出二维图纸（HLR + 标注 + 图框）是另一条线，
> 见 [路线图](ROADMAP.md)。

---

## 1. 怎么用

- 开始 → **打开图纸**（`Ctrl+Shift+O`），或"打开"对话框里的 `图纸` 过滤项。
- 打开后是一个独立的二维页签，标题是文件名，进"最近打开"列表（带缩略图）。
- 图纸页是**只读**的：`Ctrl+S` 只会提示"没有需要保存的内容"，不会写回原文件。

| 操作 | 键 |
|---|---|
| 缩放 | 滚轮（以光标为中心） |
| 平移 | 按住左键 / 中键拖拽 |
| 适配窗口 | `F` 或双击左键 |
| 100% 缩放 | `Ctrl+0` |
| 翻页（多页 PDF） | `PageUp` / `PageDown` |
| 右键菜单 | 适配 / 缩放 / 翻页 / 浅色背景 / 图层开关 |

左下角常驻读出**图纸绝对坐标**（DXF 归一化时减掉的原点已经加回去了），右下角是缩放比与页码。

## 2. 支持到什么程度

| 格式 | 实现 | 说明 |
|---|---|---|
| 位图 | Qt `QImageReader` | PNG / JPG / BMP / TIFF / GIF / WebP |
| SVG | `QSvgRenderer` 矢量直绘 | 放大不糊 |
| DXF | 自研 ASCII 解析器（`src/engine/drawing/`） | 见下面的实体清单 |
| PDF | `QPdfDocument`（可选依赖） | 按缩放按需光栅化并缓存 |

DXF 解析器支持的实体：

`LINE` · `CIRCLE` · `ARC` · `LWPOLYLINE`（含 bulge 凸度）· `POLYLINE`/`VERTEX` · `ELLIPSE` ·
`TEXT` · `MTEXT` · `INSERT`（块引用，含嵌套、缩放、旋转、阵列）

图层表（`TABLES`/`LAYER`）会读进来，用于图层颜色与右键里的图层开关。颜色支持
`62`（ACI 索引，1–9 精确，其余按 24 色相 × 10 档近似）与 `420`（真彩色），`BYLAYER` 取图层色。

**没做**：`SPLINE` / `HATCH` / `DIMENSION` / `SOLID` / 三维实体 / `XREF` 外部参照——遇到就计入
"未支持"个数并在状态栏报出来，不会让整个文件失败。二进制 DXF 与 DWG 会明确拒绝并提示。

翻模（图纸 → 墙 / 柱 / 门窗）要读的不止曲线，所以解析器还带上这些：`$INSUNITS`（图纸单位换算）、
图元类型（`LINE` / `CIRCLE` / `ARC` …）、块名（`M0921` 这类门窗编号）、图元标高（组码 30/38），
以及块内画在 0 层的图元继承块引用图层。它们挂在 `DrawingPath` 上，看图用不到，翻模离不开。
详细流程见 [图纸 → BIM](DRAWING-TO-BIM.md)。

## 3. 两条硬约定

**世界坐标 Y 向上。** DXF / 工程图是 Y 向上的，位图和 SVG 是 Y 向下的。统一做法：视图的世界变换里
带一次 Y 翻转（`scale(zoom, -zoom)`），位图/SVG/PDF 在页矩形里再翻一次抵消，DXF 直接画。
两处翻转必须成对，动其中一处要一起看。

**引擎不依赖 Qt。** DXF 解析产出的是 `Drawing`（`src/engine/drawing/`，纯 std），画由 app 层用
`QPainter` 完成。所以解析器能在 `tamias_tests` 里 headless 测（`tests/dxf_reader_tests.cpp`），
也能在没有 Qt 的 WASM 构建里编出来。

## 4. 代码地图

| 文件 | 角色 |
|---|---|
| [dxf_reader.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/drawing/dxf_reader.cpp) | 组码分词、块表、实体 → `Drawing` 曲线/文字 |
| [drawing.h](https://github.com/terry-chao/tamias/blob/main/src/engine/drawing/drawing.h) | 2D 图纸数据模型（路径 / 文字 / 图层 / 包围盒 / 归一化原点） |
| [drawing_document.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/drawing_document.cpp) | 按扩展名分派四种加载器；按图层合成 `QPainterPath`；缩略图 |
| [drawing_view.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/drawing_view.cpp) | 缩放/平移/翻页/图层开关、坐标读出 |

样例：[`assets/samples/drawings/floor-plan-sample.dxf`](https://github.com/terry-chao/tamias/blob/main/assets/samples/drawings/floor-plan-sample.dxf)
（4 个图层、块引用门、凸度多段线、圆、文字）。

## 5. 已知限制

- **PDF 需要 Qt6::Pdf**，而它在 Qt 6 下是 GPLv3 / 商业双许可（LGPL 不覆盖），所以默认不带进构建：
  `src/app/CMakeLists.txt` 里 `find_package(Qt6 QUIET COMPONENTS Pdf)` 找到就启用，找不到就提示改用
  DXF/SVG/图片。要走宽松许可就得换 pdfium（BSD-3），那是另一件事。
- **DWG 不支持**，也不建议自研：格式闭源，只有 ODA / RealDWG（收费）或 LibreDWG（GPL）三条路。
  现实做法是让交付方出 DXF/PDF，或在转换层把 DWG 转成 DXF。
- **DXF 文字编码**：先按 UTF-8 解，出现替换字符再退回系统 ANSI 代码页（中文 Windows 上是 GBK/CP936）。
  两边都对不上才是乱码；SHX 大字体（工程字）无法还原成系统字体，字形会和 AutoCAD 里的不同。
- **大图纸**：曲线按"图层 + 颜色"合并成 `QPainterPath`，几万条曲线够用；再大需要分块 + 空间索引，
  现在没做。

## 6. 下一步

把图纸真正当"底图"贴在模型上（而不是并排看）：需要一个图片平面实体 + 无光照贴图管线，再补
比例/旋转/原点对齐的交互；这一层要改渲染管线（现在只有真实模式的 PBR 光照路径），不在本次范围内。
