# 第 7 章　命令与撤销：一切编辑都是命令

> 本章目标：理解「编辑 = 命令」的架构，看懂 `Command` 接口、命令注册、交互式命令，以及它和几何重算怎么接上。

## 7.1 为什么编辑要命令化

如果每个按钮直接在 `Document` 里改数据，撤销和重做就要自己记「改之前长什么样」——很快就会失控。把每个编辑操作包成一个 **Command 对象**，就能统一得到：

- 撤销 / 重做（每个命令自证可逆）
- 历史栈（一次操作一个命令）
- 可测试（命令不碰 UI）
- 交互流程统一（点几下、喂几个点、再执行）

## 7.2 Command 接口

打开 [`command.h`](https://github.com/terry-chao/tamias/blob/main/src/command/command.h)：

```cpp
class Command {
 public:
  virtual Result<void> execute() = 0;
  virtual void undo() = 0;
  virtual void redo() = 0;

  virtual bool interactive() const { return false; }       // 需要交互输入？
  virtual Result<bool> on_point(Vec3 point);               // 喂一个交互点
  virtual Result<bool> on_pick(Vec3 point, uint64_t picked_entity_id); // 带拾取
  virtual std::vector<Vec3> preview_polyline(Vec3 cursor); // 画预览线
};
```

三种命令一目了然：

| 命令 | execute 干什么 | undo 干什么 |
|---|---|---|
| `CreatePrimitiveCommand` | 构造实体 + 网格，`add_entity` | 从场景移除，恢复 id |
| `SetFeatureParamCommand` | 改配方参数 + 重算网格 | 改回旧参数 + 重算 |
| `MoveEntitiesCommand` | 改世界变换 | 改回旧变换 |

## 7.3 命令从哪来：注册表

启动时 `register_commands()`（[register_commands.cpp](https://github.com/terry-chao/tamias/blob/main/src/command/register_commands.cpp)）把所有命令注册进 `CommandSystem`。工具条上的按钮 → 选一个命令类型 → 进入交互模式 → 输入齐了 → `execute()`。

## 7.4 交互式命令：以「拖墙」为例

`CreateWallCommand` 是交互式的：

```
选墙工具
  → 命令进入 interactive 模式
    → 第一个点（起点）→ 有预览线了
      → 第二个点（终点）→ on_point 返回「输入齐了」
        → execute()：算长度/中点/朝向 → 写配方 → add_entity
```

视口靠 `preview_polyline()` 在拖拽时画「起点→光标」的预览线；窗口/门上放时走 `on_pick`，命中墙就带上宿主 id（第 9 章 BIM 用这个）。

## 7.5 一个完整闭环：改参数

```
属性面板改 depth
  → SetFeatureParamCommand
    → entity->model.set_param(feature_id, "depth", 2.0)
      → geometry_builder().build(entity->model)   // 重算（第 5 章）
        → asset->cpu = 新三角网                    // 同一 mesh id
          → recompute_scene()
```

**注意命令本身不含几何细节**：它只改配方、调 builder、通知刷新。几何规则全在 modeling 层——这保持了「每一层只干一件事」。

## 7.6 新手常问：撤销栈在哪

命令栈（undo/redo 栈）是 `CommandSystem` 的职责；当前 `history.h` 里还有全量快照兜底。将来几何编辑多了，撤销粒度会细到单条命令（路线图 P2）。

## 7.7 动手练习

1. 读 [`create_wall_command.cpp`](https://github.com/terry-chao/tamias/blob/main/src/command/create_wall_command.cpp)，标出它实现了 `Command` 的哪些虚函数。
2. 放一个盒子 → 改参数 → `Ctrl+Z` 撤销，观察属性面板和视口的变化。
3. 在 `Command::execute()` 打日志，看一次拖墙打了哪几条命令。

## 延伸阅读

- [Qt 壳](../APP.md)：命令和 app 的接缝
- [插件系列](../plugin/index.md)：C# 插件同样走 `dispatch`，不另开编辑通道
- [关联关系](../bim/relations.md)：`SetFeatureParamCommand` 之后 BIM 怎么被通知
- [特征树求值器](../FEATURE-TREE-EVALUATOR.md) 第 1.5 节：改参数闭环

下一章：[渲染管线](08-rendering-pipeline.md)
