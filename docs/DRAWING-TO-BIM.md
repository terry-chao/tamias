# 翻模：图纸 → BIM 模型

> 把 DXF 平面图读成墙 / 柱 / 门窗，建成**参数化**构件（不是死网格）。
> 定位是**半自动**：识别给草案，人做复核。识别是启发式的，一定会猜错——所以流程里
> 有一张候选表，而不是一个"一键完成"按钮。

---

## 1. 流程

```
DXF 文件
  → Drawing（二维曲线 + 图层 + 块名 + 标高，见 参考图纸）
    → 规则识别（build_drawing_import_plan）        ← 纯 std，无头可测
      → DrawingImportPlan（墙 / 柱 / 门窗候选 + 置信度 + 来源 + 警告）
        → 人工复核（翻模对话框：勾选、改图层映射、改默认尺寸）
          → ImportDrawingCommand（一条命令 = 一步撤销）
            → Document（参数化构件 + 门窗宿主关联）
```

入口：**开始 → 翻模**（`Trace Drawing`）。停在图纸页时会自动带出那张 DXF。

---

## 2. 两条硬约定

**平面坐标 → 世界坐标。** 图纸 (x, y) 映射到世界 (x, elevation, y)：Y 是高度，地面是 XZ 平面。
这与相机一致（`TurntableCamera` 注释：*Plan: screen X = world +X, screen up = world +Z (drawing Y)*）；
反过来映射会让平面图镜像。注意多边形轮廓喂给 OCCT 时要取负 y——OCCT 是 Z-up，装配时按
`(x,y,z)→(x,z,-y)` 翻转，两处约定必须成对看。

**图纸绝对坐标。** 解析 DXF 时为了浮点精度会把包围盒挪到原点，减掉的量记在 `Drawing::world_origin()`。
翻模要把它**加回去**：构件落在图纸绝对坐标上，才能和用户照着这张图画的轴网对得上
（图纸视图左下角显示的也是绝对坐标）。

单位同理：`$INSUNITS` 决定图纸单位 → 米。没写就按米算并给出警告——**"12000" 是毫米还是米
只能靠这一条**，图纸没写时由用户在对话框里指定。

---

## 3. 识别规则（现状）

| 构件 | 判据 | 置信度 |
|---|---|---|
| 墙 | 命中墙图层、不在块内、直线段且长度 ≥ 0.3 m；闭合多段线按每条边拆一面墙 | 1.0 |
| 柱（圆） | 命中共图层、图元是 `CIRCLE`，直径落在 0.1–3.0 m | 1.0 |
| 柱（矩形） | 命中共图层、闭合多段线 4–8 点，外接框两边都落在 0.1–3.0 m | 0.7（可能不是柱） |
| 门窗 | 命中门窗图层，或块名像门窗（`M0921` / `C1518`）；同一块名且相邻的图元聚成一簇 | 1.0（有块名）/ 0.6（只有图层） |

细节：

- **图层匹配**按「包含」做（忽略大小写）：`墙` 能命中 `剪力墙`。默认 `wall,墙` / `column,col,柱` /
  `door,window,门,窗`，可在对话框里改。
- **门窗尺寸**取簇的外接框长边（0.4–4.0 m 之间才采信，否则用默认值）。块名首字母 `M`+数字判门、
  `C`+数字判窗——这是国产图纸的编号约定。
- **宿主墙**：门窗中心到墙中线的距离 ≤ 墙厚/2 + 容差（默认 0.35 m），且投影不超出墙端 0.5 m。
  找不到宿主的候选会被丢弃并记进警告——它宁可不建，也不建到错误的墙上。
- **轴网吸附**：墙端点吸附到轴网（容差默认 0.25 m），吸附在**世界坐标**里做。门窗不单独吸附：
  它的位置由宿主墙决定。
- **块内图层 0 继承块引用的图层**（DXF 语义）：不处理的话门窗符号会落到名为 "0" 的图层上，就找不到了。

---

## 4. 落地

`ImportDrawingCommand` 按「墙 → 柱 → 门窗」建构件：门窗经 `bind_opening_to_host` 绑到刚建的墙上
（写 `HostedOn` 关联、切穿墙身）。整次翻模是**一条命令**，所以一次撤销整体回退、一次重做整体恢复
（redo 复用同一批句柄，不重新识别）。

复核里把某面墙取消勾选时，挂在这面墙上的门窗会被一并剔除，剩下的门窗宿主下标会重映射——
这条规则在 `filter_drawing_import_plan` 里，有单测覆盖。

---

## 5. 现状与限制

**做了：** 墙、柱、门窗；单位；轴网吸附；人工复核；一步撤销。

**没做（按价值排）：**

1. **梁**：截面尺寸不在几何里，写在平法标注文字里（`KL1(3) 300x600`）。要解析标注文字并与梁线配对，
   工作量比其余构件加起来还大。
2. **板**：需要先判定闭合外轮廓（现在图层上所有线都被当墙），且板要支持任意多边形轮廓。
   `FeatureKind::PolygonProfile` 已经能做，缺的是"哪个闭合环是外轮廓"这一步。
3. **双线墙**：现在按单线（轴线）处理，厚度取默认值。真实建筑图墙体多是双线，需要平行线配对
   求中线 + 厚度。
4. **曲线墙**：圆弧 / 圆 / 椭圆上的墙段会被跳过并给出警告。
5. **DWG**：不支持，也不建议自研（格式闭源）。现实做法是让交付方出 DXF，或在转换层把 DWG 转成 DXF。
6. **HATCH / SOLID**：结构图里柱和剪力墙常是实心填充，没有填充就只能靠闭合轮廓猜。

---

## 6. 代码地图

| 文件 | 角色 |
|---|---|
| [drawing_import.h](https://github.com/terry-chao/tamias/blob/main/src/bim/drawing_import.h) | 候选 / 选项 / 计划的纯数据定义 |
| [drawing_import.cpp](https://github.com/terry-chao/tamias/blob/main/src/bim/drawing_import.cpp) | 规则识别（墙 / 柱 / 门窗 + 宿主匹配 + 轴网吸附） |
| [import_drawing_command.cpp](https://github.com/terry-chao/tamias/blob/main/src/command/import_drawing_command.cpp) | 计划 → 文档（一条命令、一步撤销） |
| [drawing_import_dialog.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/drawing_import_dialog.cpp) | 复核界面：图层映射、默认尺寸、候选表勾选 |
| [dxf_reader.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/drawing/dxf_reader.cpp) | 解析器：单位 / 图元类型 / 块名 / 标高 / 块内图层继承 |
| [grid.h](https://github.com/terry-chao/tamias/blob/main/src/bim/grid.h) | 轴网（翻模的定位基准） |

测试：

- [`tests/drawing_import_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/drawing_import_tests.cpp)：
  用仓库里的 `floor-plan-sample.dxf` 做金样——12 m × 8 m 的平面图应识别出 6 面墙、3 根 Ø600 圆柱、
  1 樘 900 宽的门（挂在 y = 4000 那道内墙上）；另有轴网吸附、复核过滤、命令撤销/重做。
- [`tests/dxf_reader_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/dxf_reader_tests.cpp)：
  单位、图元类型、标高、块名与块内图层继承。

---

## 7. 下一步

1. **双线墙**：平行线配对求中线 + 实测厚度，这是把"能看出个形状"变成"尺寸对得上"的关键一步。
2. **板**：闭合环判定 + `PolygonProfile` 楼板（几何内核已经就绪）。
3. **梁**：平法标注解析（文字 → 截面），单独一条线。
4. **复核界面第二步**：点候选 → 三维里高亮对应构件，改完再看效果；现在是整表勾选。
