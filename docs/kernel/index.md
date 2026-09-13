# 几何边界

几何边界属于 [造型](../modeling/index.md)，不是单独一层。内核插件口：上面的层不出现 `TopoDS_Shape`。OCCT 和将来的 IfcOpenShell 都从这里接。

- [建模内核（Kernel）](../MODELING-KERNEL.md) —— 接口层怎么分（`ModelKernel` 动词 + 注册表），
  后端怎么接（OCCT，将来 ACIS），中间层与后端的边界在哪
- [IShapeOps 与 OCCT](../ISHAPE-OPS.md)
