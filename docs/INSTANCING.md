# 合批 / Instancing

> 大 BIM 的下一道性能命门。语义树 / 展平见 [SCENE-GRAPH.md](SCENE-GRAPH.md)；视锥一期见 [视锥剔除](FRUSTUM-CULLING.md)；几十亿三角的总图见 [超大规模三角](MASSIVE-GEOMETRY.md)（本文是其中 **G1** 的企业级方案）。

**状态：** G1a（几何 intern）+ G1b（实例顶点缓冲 + shader 读实例）+ G1c（`RecordCommands` 按 `BatchKey` 分桶，`instance_count = N`）已落地。半透明仍一物一 draw。G1 完成前不要开阴影 pass、不要做 Nanite。

---

## 0. 一句话

企业级合批不是把 `instance_count` 改成 N。Navisworks / CATIA CGR / JT / Revit Family / Cesium 3D Tiles 做的是同一件事：**先认出「这是同一份几何」，再在 GPU 上用实例表提交。** Draw 是最后 10%。

Tamias 今天每个可见叶子一次 push constant + `draw_indexed`（`instance_count = 1`），且 `add_entity` 总是 `new` 一份 `MeshAsset`。一万根同型号柱 = 一万份 CPU 网 + 一万次提交。

---

## 1. 三层，不要做成一层

| 层 | 干什么 | 谁受益 |
|---|---|---|
| **L0 几何身份** | 指纹 intern，一份 `MeshAsset` 被 N 个节点引用 | 同型号柱、门类型、IFC MappedItem |
| **L1 GPU instancing** | 同网格一次 `draw_indexed(N)` | L0 已经共享的那些 |
| **L2 管线合批 / MDI** | 不同网格、同一材质/PSO，Indirect 一次提交多份 | **不同长度的墙**（L1 帮不上） |

只做 L1 会得到假结论：「墙合不了，合批没用」。墙本来就几乎不是同一份几何。柱、家具、门类型才是 L1 的主战场；唯一墙留给 L2。G1 做 L0+L1；L2 是 G6。

```
Scene 实例（node → geometry_id + world）
        │
        ▼
   L0 intern（指纹，一份网 N 引用）
        │
        ▼
   可见性（一期视锥，G1 不动）
        │
        ▼
   L1 CPU 分桶（BatchKey → Instance[]）
        │
        ▼
   实例缓冲（world 3×4 + 颜色 + node_id）
        │
        ▼
   一次 draw N（L2 以后才上 MDI）
```

四条不变的规则（与 [MASSIVE-GEOMETRY.md](MASSIVE-GEOMETRY.md) 一致）：

1. **语义节点不持三角。** `SceneNode` 只引用 `mesh_asset_id`。
2. **一份几何，N 个实例。** 变换和材质 override 在实例上。
3. **三角是可重建缓存。** 改参数 → copy-on-write intern 新网，不要原地改还被别人引用的资产。
4. **合批不要按语义父子。** 楼层是剪枝用的，不是 draw 分组键。

---

## 2. 合批键 vs 实例数据

**进键（相同才能合）：**

| 字段 | 原因 |
|---|---|
| `gpu_mesh_id` / LOD | 顶点缓冲必须相同才能 instance |
| pipeline（线框 / 着色 / 真实 / 混合 / 线） | 切 PSO 比 draw 贵 |
| albedo / normal 贴图 | 描述符切换；同贴图才合 |
| topology（三角 / 折线） | 草图线不要和实体三角混 |

**进实例（每份不同）：**

| 字段 | 原因 |
|---|---|
| world 3×4 | 每份位置不同；**存 world 不存 MVP** |
| base / category 颜色、不透明度 | 同网不同色是 BIM 常态 |
| roughness / metallic / selected | 选中不要拆桶，否则多选会炸批次数 |
| `node_id` | 给以后 GPU 拾取；现在拾取仍走 CPU BVH |

**不进键：** 楼层、语义父节点、GUID / Pset。语义留在 Document。

实例里存 world，不存 MVP：阴影级联、剖切、多视口换的是 FrameConstants（view / proj / eye / IBL），同一张实例表能复用。

每条实例目标 **64 字节**（打包后）。G1 用未打包的 80 字节顶点记录（三行 float4 仿射矩阵 + float4 颜色 + float4 材质，见 `gpu_instance.h`），打包是后续细节。10 万份即使按 96 B 计也只有约 10 MB。**N=1 也走同一路径**，禁止「单物体旧 push constant、多物体新 instance」两套管线。

---

## 3. 通道策略

| 通道 | G1 策略 | 原因 |
|---|---|---|
| 不透明 | 自由 instance | 顺序无关；主收益全在这里 |
| Alpha test | 并入不透明 | discard 不破坏合批 |
| 半透明（玻璃） | **不合批**，仍按深度排序单画 | 同型号玻璃在不同深度，instance 会打乱 back-to-front |
| 选中填充 | 实例 flags 里带 selected | 不要按 selected 拆 BatchKey |
| 轮廓 / 夹点 / 网格 / 轴 | 独立 overlay，永不进模型桶 | 状态完全不同 |
| 阴影（以后） | 复用本帧实例缓冲，只换 view | 所以实例里绝不能烤 MVP |

---

## 4. RHI 契约

**不新增「合批」动词。** `DrawIndexedDesc.instance_count` 三个后端已经吃了。缺的是实例缓冲 + shader 用 `SV_InstanceID` / `gl_InstanceIndex` 取值，以及把每帧常量从 per-object PushConstants 里拆出去。

| 槽 | 内容 | 更新频率 |
|---|---|---|
| Frame 常量 | view、proj、eye、exposure、IBL mip、显示模式 | 每帧一次，所有批次共享 |
| Instance 缓冲 | 本帧可见 `GpuInstance[]` | 每帧写入可见集 |
| Mesh VBO/IBO | L0 intern 后一份 | 几何变了才 upload |
| 材质贴图 | 按 BatchKey 绑定 | 切批时换，不按实例换 |

实例数据用 **顶点 instance rate**（`GpuInstance` 第二槽），不要用 push constant 数组（128–256 字节装不下）。Storage / SSBO 留给以后的 compute 剔除和 indirect。

| 后端 | G1 | 以后 |
|---|---|---|
| Vulkan | instance 顶点缓冲 + `vkCmdDrawIndexed(..., instanceCount)` | `vkCmdDrawIndexedIndirectCount` |
| OpenGL 4.5 | divisor=1 + `glDrawElementsInstanced` | `glMultiDrawElementsIndirect` |
| WebGPU | instance stepMode + `drawIndexed(instanceCount)` | 间接绘制有；不做 mesh shader |

---

## 5. L0 intern（Document，不在 RHI）

`Document::add_entity` / `add_import_mesh` 按 **网格内容指纹** 复用 `MeshAsset`。特征树哈希和 IFC Type id 以后可以当更快的前站；GPU 只关心三角是否相同。

| 来源 | 指纹 | 何时拆份 |
|---|---|---|
| 特征树求值结果 / 导入网格 | 顶点 + 索引 + `line_list` / `has_texcoord` 的稳定哈希 | 改参数、倒角、夹点落盘：copy-on-write intern 新网；旧网若仍被引用则保留 |

改参数 **禁止** `asset->cpu = new_mesh` 原地覆盖：两根共享网的柱会一起变形。走 `Document::replace_entity_mesh`。

加载旧 `.tdoc` **不合并** 已有 id（撤销/序列化要稳住句柄）。新创建的实体才 intern。

---

## 6. 分期

| 刀 | 改哪里 | 可独立验收 |
|---|---|---|
| **G1a intern** | `Document` 按指纹复用；改参数 COW | 1 万同型号柱：CPU 网格份数 = 1；画面不变 |
| **G1b 常量拆分** | 实例顶点缓冲 + `mesh.vert` 读 world/颜色；`pc.mvp` = view_proj | 现有场景观感不变；N=1 走实例路径 |
| **G1c 分桶** | `RecordCommands` 先 `BatchKey` 分桶再提交 | 同 mesh 多实例：draw 次数 = 批次数 |
| **G1d 锁测试** | Mock RHI：同 mesh 多实例的 `instance_count`；半透明不合批 | 回归不靠肉眼 |

**G1 明确不做：** 语义树剪枝、BVH 视锥、自适应 deflection、阴影、剖切、透明合批、Nanite、LevelDB、把 `Scene` 搬进 GPU。

---

## 7. 验收与反模式

1080p 着色、工作站：

- 1 万同型号柱 → draw **≤ 数次**
- 任意场景目标 draw **< 2000**
- N=1 与 N=N 同一条录制路径

反模式：

| 做法 | 为什么拒绝 |
|---|---|
| 按楼层 / 父节点分桶 | 把语义树当成渲染图；同型号柱被拆散 |
| 实例里存 MVP | 阴影、剖切、多视口每 pass 重写整表 |
| 半透明和实体一个桶 | 深度顺序错，玻璃闪 |
| 按 selected 拆键 | 框选一千个构件变成一千批 |
| N=1 走旧路径 | 永远修不完的双实现 |
| 每批 `new` 一个 GPU buffer | 分配比 draw 还贵 |
| 先做 Nanite / meshlet | 有 BRep 时重离散更便宜；G1–G3 未完不动 G5 |

---

## 源码锚点

| 文件 | 现在干什么 | G1 落点 |
|---|---|---|
| [mesh.h](https://github.com/terry-chao/tamias/blob/main/src/engine/graphics/mesh.h) | `MeshCpu` | 内容指纹 |
| [mesh_asset.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/mesh_asset.h) | 一资产一份 CPU 网 | intern + `content_hash` |
| [document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp) `add_entity` | 每次 `add_mesh` | `intern_mesh` / `replace_entity_mesh` |
| [gpu_instance.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/gpu_instance.h) | 80B 实例记录 | 仿射行 + 颜色/材质 |
| [batch_key.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/batch_key.h) | 合批键 | 网格 / PSO / 贴图 / 线 |
| [scene_graph.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.cpp) `RecordCommands` | 按 `BatchKey` 分桶，flush 时 `instance_count = N` | 半透明仍单画 |
| [device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h) | `set_instance_buffer` + `PipelineDesc.instanced` | 四后端 instance rate |
