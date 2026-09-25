# 文档快照与枚举

> 插件看到的"文档"全是**只读快照**：id + 种类 + 名字 + 特征参数。没有 `Document*`、没有 `TopoDS_Shape`、没有变换矩阵。

这一页列全快照类型和它们的枚举值。取快照的方法在 [IHost](host.md)。

---

## 1. `EntityInfo`

```csharp
public readonly record struct EntityInfo(ulong Id, EntityKind Kind, string Name);
```

| 成员 | 类型 | 说明 |
|---|---|---|
| `Id` | `ulong` | 文档内唯一的实体 id，永不为 0 |
| `Kind` | `EntityKind` | 解析失败时是 `Unknown` |
| `Name` | `string` | 实体名，可能为空串 |

值相等是按字段比较的（record struct），可以直接 `==`，也能当字典键。

```csharp
var byId = host.Entities.ToDictionary(e => e.Id);
if (byId.TryGetValue(id, out var info) && info.Kind == EntityKind.Wall) { … }
```

---

## 2. `EntityKind`

数值与 C++ `EntityKind` **同序**，只追加、不复用旧值。

| 名字 | 值 | 名字 | 值 |
|---|---:|---|---:|
| `Unknown` | -1 | `Line` | 8 |
| `Wall` | 0 | `Polyline` | 9 |
| `Box` | 1 | `Circle` | 10 |
| `Cylinder` | 2 | `Arc` | 11 |
| `Beam` | 3 | `Bezier` | 12 |
| `Column` | 4 | `Rectangle` | 13 |
| `Slab` | 5 | `BSpline` | 14 |
| `Door` | 6 | `Nurbs` | 15 |
| `Window` | 7 | | |

宿主把内核给的名字（`"Wall"`、`"Box"`…）按名字解析成枚举，**大小写不敏感**；解析不出来就是 `Unknown`。

---

## 3. `PickPoint`

```csharp
public readonly record struct PickPoint(float X, float Y, float Z, ulong EntityId);
```

| 成员 | 说明 |
|---|---|
| `X` / `Y` / `Z` | 世界坐标，**Y 向上**，单位米 |
| `EntityId` | 这个点落在哪个实体上；没落在实体上时是 `0` |

`PickPoint` 是两个方向共用的坐标类型：`CommandArgs.SetPoints(…)` 写参数，`PointInputResult.Points` / `EntityInputResult.Hits` 读结果。

```csharp
host.Wall(new PickPoint(0, 0, 0, 0), new PickPoint(5, 0, 0, 0));
```

---

## 4. `FeatureInfo` / `FeatureParam`

```csharp
public readonly record struct FeatureInfo(
    ulong Id,
    FeatureKind Kind,
    IReadOnlyList<ulong> Inputs,
    IReadOnlyList<FeatureParam> Params);

public readonly record struct FeatureParam(string Name, double Value);
```

| 成员 | 说明 |
|---|---|
| `FeatureInfo.Id` | 特征 id。改参数 / 加圆角要的就是它 |
| `FeatureInfo.Kind` | 特征种类，见下 |
| `FeatureInfo.Inputs` | 依赖的上游特征 id，**依赖在前**（拓扑序）；没有依赖时是空表 |
| `FeatureInfo.Params` | 参数，**按名字升序**（核心里是 unordered_map，宿主导出前排过序，顺序稳定） |
| `FeatureParam.Name` | 参数名，例如 `"depth"`、`"radius"`、`"height"` |
| `FeatureParam.Value` | 参数当前值。**只有 double**：没有类型、没有单位、没有取值范围 |

"这个参数能填多少"属于界面规格（`param_spec`），不在文档里，也没有读接口。插件要限制范围就自己写进对话框：`PromptForm.AddNumber(id, label, value, min, max)`。

```csharp
foreach (var feature in host.Features(entityId))
{
    var args = string.Join(", ", feature.Params.Select(p => $"{p.Name}={p.Value:0.###}"));
    var deps = feature.Inputs.Count == 0 ? "" : $" <- [{string.Join(", ", feature.Inputs)}]";
    host.Log($"#{feature.Id} {feature.Kind}({args}){deps}");
}
```

---

## 5. `FeatureKind`

数值与 C++ `FeatureKind` 同序，只追加、不复用旧值。

| 名字 | 值 | 名字 | 值 |
|---|---:|---|---:|
| `Unknown` | -1 | `Arc` | 9 |
| `RectProfile` | 0 | `Bezier` | 10 |
| `Extrude` | 1 | `RectWire` | 11 |
| `CircleProfile` | 2 | `BSpline` | 12 |
| `Boolean` | 3 | `Nurbs` | 13 |
| `Fillet` | 4 | `PolygonProfile` | 14 |
| `Chamfer` | 5 | `Transform` | 15 |
| `Line` | 6 | `Cylinder` | 16 |
| `Polyline` | 7 | | |
| `CircleWire` | 8 | | |

注意 `Line` / `Polyline` / `CircleWire` 这些名字在 `FeatureKind`（草图特征）和 `EntityKind`（草图实体）里都有，但值不一样、语义也不一样——按名字读没问题，别按数值互相比较。

---

## 6. 快照的边界

明确**读不到**的东西，省得找：

| 想读 | 现状 |
|---|---|
| 几何（网格、B-Rep、曲线控制点） | 没有接口。特征参数里也没有曲线点坐标 |
| 世界变换矩阵 / 摆放 | 没有接口 |
| 材质、颜色 | 只能写（`set_material`），不能读 |
| 轴网、文字注记 | 不在 `host.Entities` 里，也没有读接口 |
| 楼层 | 没有直读接口；可以用 `create_storey` / `set_location` 间接写 |
| 属性面板的字段规格（范围、单位） | 不在 ABI 里 |
