# 造型

特征树是配方（纯数据，不碰 OCCT）。求值器拿着配方去指挥几何边界里的内核。

**现在怎么造型**（点工具 → 写配方 → OCCT 出网 → 进文档）写在 [特征树求值器](../FEATURE-TREE-EVALUATOR.md) 第 1 节。

```
点工具 → Entity 写 FeatureModel
  → createGeom → IGeometryBuilder → evaluate_feature_model
    → BRep → tessellate → MeshCpu
      → Document::add_entity → 场景节点 + 渲染
```

改参数只改配方，整棵树重算。打开 STEP 不走这条路，没有特征树。

- [特征树求值器](../FEATURE-TREE-EVALUATOR.md) —— 当前实现：配方、求值、改参数闭环
- [MCAD 管线](../MCAD-PIPELINE.md) —— 还没做的深路径：草图约束 → 挤出 → 倒角/布尔 → 拓扑命名
