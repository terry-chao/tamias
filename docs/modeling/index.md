# 造型

特征树是配方，求值器拿着配方去指挥内核。几何边界（`IShapeOps` / OCCT）也算这一类：上面的层不出现 `TopoDS_Shape`，读 STEP、执行 BRep 都从这里走。

**现在怎么造型**（点工具 → 写配方 → OCCT 出网 → 进文档）写在 [特征树求值器](../FEATURE-TREE-EVALUATOR.md) 第 1 节。

```
点工具 → Entity 写 FeatureModel
  → createGeom → IGeometryBuilder → evaluate_feature_model
    → BRep → tessellate → MeshCpu
      → Document::add_entity → 场景节点 + 渲染
        （BIM 构件将来经 BimModel 再挂楼层，见 BIM 业务层）
```

改参数只改配方，整棵树重算。打开 STEP 不走特征树，只走几何边界的 `IShapeOps::read_file`。

- [特征树求值器](../FEATURE-TREE-EVALUATOR.md) —— 当前实现：配方、求值、改参数闭环
- [MCAD 管线](../MCAD-PIPELINE.md) —— 还没做的深路径：草图约束 → 挤出 → 倒角/布尔 → 拓扑命名
- [几何边界](../ISHAPE-OPS.md) —— 内核插件口：OCCT；IFC 将来也走这里
