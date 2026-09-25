# 2. 读文档

> 插件看到的是**快照**：实体列表、选择、特征树和参数。这一章用它们做两个真命令——「列出选择」和「列出特征」。

全部读接口只有四个属性/方法（[IHost](../api/host.md)）：

| 成员 | 给你什么 |
|---|---|
| `host.DocumentName` | 当前文档名（没有文档时是空串） |
| `host.Entities` | 全部实体，`EntityInfo` 列表，id 升序 |
| `host.Selection` | 当前选中的 id 列表，按选择顺序 |
| `host.Features(id)` | 某个实体的特征树，`FeatureInfo` 列表 |

没有 `GetEntity()`，没有句柄——读到的每一项都是值类型或只读列表。

---

## 1. 列出选择

```csharp
host.AddCommand("my.list_selection", "列出选择", () =>
{
    var sel = host.Selection;
    if (sel.Count == 0)
    {
        host.Log("未选择对象");
        return;
    }
    var entities = host.Entities.ToDictionary(e => e.Id);
    foreach (var id in sel)
    {
        host.Log(entities.TryGetValue(id, out var info)
                     ? $"#{info.Id} {info.Kind} {info.Name}"
                     : $"#{id}（已不在文档里）");
    }
}, "把当前选择写到状态栏");
```

两个必须养成的习惯：

1. **先判空**。没选东西、没文档是常态，不要让它异常。
2. **配字典查名字**。`Selection` 只给 id，`Entities` 才有种类和名字；`ToDictionary` 一次建好，循环里 O(1)。

`EntityInfo` 是 `record struct`，所以 `==` 是按字段比、也能当字典键。[文档快照与枚举](../api/document.md) 里有完整字段和 `EntityKind` 表。

---

## 2. 列出特征与参数

`host.Features(entityId)` 返回该实体的特征树（只读）。实体不存在时**返回空表**，不抛异常。每条 `FeatureInfo` 给四件事：

```csharp
host.AddCommand("my.list_features", "列出特征", () =>
{
    var id = host.Selection.FirstOrDefault();
    if (id == 0)
    {
        host.Log("未选择对象");
        return;
    }

    var features = host.Features(id);
    if (features.Count == 0)
    {
        host.Log($"#{id} 没有特征树");
        return;
    }

    host.Log($"#{id} 特征 {features.Count} 条");
    foreach (var feature in features)
    {
        var args = string.Join(", ", feature.Params.Select(p => $"{p.Name}={p.Value:0.###}"));
        var deps = feature.Inputs.Count == 0
            ? ""
            : $" <- [{string.Join(", ", feature.Inputs)}]";
        host.Log($"  #{feature.Id} {feature.Kind}({args}){deps}");
    }
}, "把选中实体的特征树和参数写到控制台");
```

输出大概长这样：

```
#12 特征 2 条
  #13 RectProfile(width=0.4, depth=0.4) <- []
  #14 Extrude(depth=3, height=3) <- [13]
```

要点：

| 成员 | 语义 |
|---|---|
| `feature.Id` | **改参数要的就是它**。以前只能猜，现在能枚举 |
| `feature.Kind` | `FeatureKind`，例如只想改拉伸就 `Where(f => f.Kind == FeatureKind.Extrude)` |
| `feature.Inputs` | 上游特征 id（依赖在前）；上面的 `<- [13]` 表示这条拉伸吃的是 13 号草图 |
| `feature.Params` | 参数，**按名字升序**，顺序稳定 |
| `param.Name` / `param.Value` | 参数名 + 当前值。**只有 double**：没有类型、没有范围 |

参数能填多少属于**界面规格**（`param_spec`），不在文档里。要在插件里限制范围就自己写进对话框（下一章会用 `PromptForm.AddNumber(…, min, max)`）。

---

## 3. 快照不是句柄：边改边读要重新取

这一条踩一次就记住了：

```csharp
// 反例：边删边读，删到一半列表就过期了
foreach (var id in host.Selection)
{
    host.Dispatch("delete_entity", new CommandArgs().SetInt("entity_id", (long)id));
}

// 正解：先拷一份
foreach (var id in host.Selection.ToList())
{
    host.Dispatch("delete_entity", new CommandArgs().SetInt("entity_id", (long)id));
}
```

`host.Entities` / `host.Features(id)` 同理：它们是**调用那一刻**的结果，不是会自己更新的视图。

---

## 4. 什么时候拿不到数据

| 情况 | 表现 |
|---|---|
| 停在欢迎页（没有活动文档） | `DocumentName` 是空串，`Entities` / `Selection` 是空表 |
| 实体 id 不存在 | `Features(id)` 返回空表 |
| 实体没有特征树 | `Features(id)` 返回空表（上面已经处理了） |
| 想读几何 / 矩阵 / 材质 / 轴网 / 文字 | 没有接口，见[快照的边界](../api/document.md) |

---

下一章：[3. 改文档](03-edit-and-undo.md)（`Dispatch` + 事务）
