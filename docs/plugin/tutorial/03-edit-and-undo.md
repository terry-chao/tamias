# 3. 改文档

> 插件**没有**写接口：改文档只能 `Dispatch` 一条已经存在的命令。这一章讲怎么发命令、参数怎么写，以及怎么让一批修改只占一步撤销。

---

## 1. 一条命令就是一次编辑

```csharp
host.Dispatch("delete_entity", new CommandArgs().SetInt("entity_id", (long)id));
```

`Dispatch(命令名, 参数)` 和点工具条走的是同一条路：

```
插件 → Dispatch → CommandSystem.dispatch → Command::execute → 进撤销栈 → 刷新视口
```

所以插件里做的事，用户都能一次 `Ctrl+Z` 退回去。

三条硬规则：

| 规则 | 说明 |
|---|---|
| 命令名必须已注册 | 拼错就抛 `InvalidOperationException`，状态栏写 `unknown command '…'` |
| 参数类型要对上 | 内核按 variant 取参数：`arg_int` **不认** double。id 一律 `SetInt` |
| 别 new 实体 | 没有 `Document*`，也没有几何对象；要建东西就发 `create_*` |

命令全集（创建、编辑、轴网、文字、材质）见[命令与参数](../api/commands.md)。

---

## 2. 删所选

```csharp
host.AddCommand("my.delete_selected", "删除所选", () =>
{
    var ids = host.Selection.ToList();   // 先拷一份：边删边读会看到过期列表
    if (ids.Count == 0)
    {
        host.Log("未选择对象");
        return;
    }
    foreach (var id in ids)
    {
        host.Dispatch("delete_entity", new CommandArgs().SetInt("entity_id", (long)id));
    }
}, "对当前选择逐个 dispatch delete_entity");
```

注意这里是**每个实体一条撤销记录**——用户按了 5 次删除就要按 5 次 `Ctrl+Z`。想合成一条，用下一节的事务。

---

## 3. 按名字改参数

改尺寸用 `set_param`，要三样东西：`entity_id`、`feature_id`、参数名。上一章已经能枚举出来：

```csharp
host.AddCommand("my.set_depth", "改拉伸深度", () =>
{
    var ids = host.Selection.ToList();
    if (ids.Count == 0)
    {
        host.Log("未选择对象");
        return;
    }
    if (host.Ui.PromptNumber("改拉伸深度", "深度 (m)", 3.0, 0.01, 50.0) is not double depth)
    {
        return;   // 用户取消
    }

    var changed = 0;
    foreach (var id in ids)
    {
        foreach (var feature in host.Features(id).Where(f => f.Kind == FeatureKind.Extrude))
        {
            if (!feature.Params.Any(p => p.Name == "depth"))
            {
                continue;   // 这条拉伸没有 depth 参数，跳过
            }
            host.Dispatch("set_param", new CommandArgs()
                .SetInt("entity_id", (long)id)
                .SetInt("feature_id", (long)feature.Id)
                .SetString("param_name", "depth")
                .SetDouble("value", depth));
            ++changed;
        }
    }
    host.Log(changed == 0 ? "没找到带 depth 的拉伸特征" : $"已改 {changed} 处深度");
});
```

这就是「枚举 → 改」的标准套路：不猜 id，不看属性面板抄名字。

---

## 4. 让一批修改只占一步撤销

上面的 `set_depth` 每改一条就是一步撤销。用 `BeginTransaction` 包起来：

```csharp
var changed = 0;
using var tx = host.BeginTransaction("改拉伸深度");
foreach (var id in ids)
{
    foreach (var feature in host.Features(id).Where(f => f.Kind == FeatureKind.Extrude))
    {
        host.Dispatch("set_param", new CommandArgs()
            .SetInt("entity_id", (long)id)
            .SetInt("feature_id", (long)feature.Id)
            .SetString("param_name", "depth")
            .SetDouble("value", depth));
        ++changed;
    }
}
tx.Commit();   // 整批 = 一条撤销记录
host.Log($"已改 {changed} 处深度，一步撤销");
```

语义是**显式提交**：

| 情况 | 结果 |
|---|---|
| `tx.Commit()` | 这一段命令合成**一条**撤销记录 |
| 不提交就 `Dispose()`（包括异常逃出 `using`） | 整段**回滚**，撤销栈不留记录 |
| 事务里一条命令都没发 | 什么都不发生 |
| 想嵌套开第二个事务 | 报错 |
| 事务里发交互式命令（点没给全） | 报错——点齐的时刻由鼠标决定，不在事务窗口里 |

**控制台脚本不用自己开事务**：每段脚本宿主已经替你包了一个，脚本里再 `BeginTransaction` 会直接报错。

细节见 [IHost §4 事务](../api/host.md)。

---

## 5. 参数怎么写

`CommandArgs` 是链式 setter，每种类型一个前缀：

```csharp
new CommandArgs()
    .SetInt("entity_id", 7)          // i:entity_id=7
    .SetDouble("radius", 0.25)       // d:radius=0.25
    .SetString("param_name", "depth")// s:param_name=depth
    .SetVec3("origin", 1, 2, 3)      // v:origin=1,2,3
    .SetPoints("points", [a, b])     // p:points=1,2,3|4,5,6
    .SetDoubles("weights", [1, 2.5]) // a:weights=1|2.5
```

两个最容易踩的：

1. **别依赖"无前缀自动推断"**。`entity_id=1.0` 会被当成 double，内核要 int64 就取不到。
2. **字符串里不能有 `;`**，它会被当成参数分隔符。

完整对照和已知坑见[命令与参数 §1](../api/commands.md)。

---

## 6. 建东西也一样

创建类命令给了点就立即执行，没给点就武装成交互工具（等用户在视口点）。脚本里一定要把点喂全：

```csharp
host.Wall(new PickPoint(0, 0, 0, 0), new PickPoint(5, 0, 0, 0), thickness: 0.2, height: 3);
```

下一章会用「先弹表单问尺寸，再让用户在视口点两点」的正规流程来做同一件事。

---

下一章：[4. 对话框与视口输入](04-dialogs-and-input.md)
