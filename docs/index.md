# Tamias

跨 MCAD / BIM 的**几何查看 + 参数化编辑内核**。

- **Qt** 做壳，自研 RHI 双后端渲染（Vulkan 主 / OpenGL 副）
- **OCCT** 提供 BRep 几何内核
- **特征树 + 求值器** 做参数化编辑：改参数 → 重算 → 渲染

## 文档导航

- [路线图](ROADMAP.md) —— 定位、架构决策与里程碑
- [特征树求值器与 OCCT](FEATURE-TREE-EVALUATOR.md) —— 参数化内核的数据流
- [MCAD/BIM 决策](DECISION-MCAD-BIM.md)
- [MCAD 管线](MCAD-PIPELINE.md)

## 构建与运行

见 [BUILD.md](https://github.com/terry-chao/tamias/blob/main/BUILD.md) 与 [README](https://github.com/terry-chao/tamias)。

## 源码

[github.com/terry-chao/tamias](https://github.com/terry-chao/tamias)
