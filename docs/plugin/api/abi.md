# C ABI

> 插件与宿主之间的**稳定面**是这张 C 函数表，不是 C++ 类布局，也不是 C++/CLI。
> C# 这层（`Tamias.Api`）是它的强类型包装；以后要接 Rust / 纯 C 插件，对同一张表就行。

头文件：[`src/plugin/host_api.h`](https://github.com/terry-chao/tamias/blob/main/src/plugin/host_api.h) ·
C# 侧：[`HostApi.cs`](https://github.com/terry-chao/tamias/blob/main/plugin-sdk/csharp/Tamias.Api/HostApi.cs)

```c
struct HostApi {
  std::int32_t abi_version;   // = kHostApiVersion，目前 8
  void* context;
  // …函数指针，见下
};
```

`LayoutKind.Sequential` 与之对齐（x64 上 int32 后面有 padding，别手动打包）。

---

## 1. 版本

| 版本 | 追加了什么 |
|---:|---|
| v4 及以前 | 日志、文档名、实体列表、选择、`dispatch`、注册命令 / 插件 |
| **v5** | 写选择 `set_selection`；宿主对话框 `show_dialog`；实体拾取与更丰富的点输入预览（`begin_point_input` 末尾加 `filter_kind`）；创建类命令在点列 / `origin` 给齐时改为立即执行 |
| **v6** | 宽读面：`entity_feature_count` / `entity_feature_at` / `feature_input_at` / `feature_param_count` / `feature_param_at`（全部只读，不暴露 `FeatureModel` / `TopoDS_Shape`） |
| **v7** | 事务：`begin_transaction` / `commit_transaction` / `abort_transaction` |
| **v8** | `unregister_plugin`（自动重载时"先摘旧的"那一步） |

规则：**只在表尾追加字段，并把 `kHostApiVersion` 加一**；不要在中间插。
C# 侧 `Bootstrap.Initialize` 对不上版本号就拒绝加载——宁可没有插件，也不要按错位的表调用。

两处版本号必须一起改：C++ `host_api.h` 的 `kHostApiVersion`，C# `HostApiVersion.Current`。

---

## 2. 函数表

| 字段 | 签名 | 说明 |
|---|---|---|
| `log` | `(ctx, level, utf8)` | 打日志；C# 侧固定传 `level = 0`，非 0 是宿主自己内部用的错误档 |
| `document_name` | `(ctx, buf, cap) -> int` | 填文档名 |
| `entity_count` | `(ctx) -> int` | 实体个数（id 升序） |
| `entity_id_at` | `(ctx, index, out id) -> int` | 第 index 个实体 id |
| `entity_kind` | `(ctx, id, buf, cap) -> int` | 填种类名（`"Wall"`…） |
| `entity_name` | `(ctx, id, buf, cap) -> int` | 填实体名 |
| `selection_count` | `(ctx) -> int` | 选择个数 |
| `selection_id_at` | `(ctx, index, out id) -> int` | 第 index 个选中 id |
| `dispatch` | `(ctx, command, args) -> int` | 发命令，成功 0 |
| `register_command` | `(ctx, id, title, tooltip, page, group, icon, order, flags) -> int` | `flags & 1` = 可选中 |
| `register_plugin` | `(ctx, id, title, author, version, date, desc, homepage, icon, flags) -> int` | `flags & 1` = 内置 |
| `begin_point_input` | `(ctx, request_id, min, max, flags, work_plane_y, preview_kind, preview_curve_kind, filter_kind) -> int` | 见 [§3](#3) |
| `cancel_point_input` | `(ctx, request_id) -> int` | 取消 |
| `set_selection` | `(ctx, ids, count) -> int` | 写选择，`count = 0` 即清空 |
| `show_dialog` | `(ctx, kind, buttons, spec, out, cap) -> int` | 见 [§4](#4) |
| `entity_feature_count` | `(ctx, entity_id) -> int` | 特征条数 |
| `entity_feature_at` | `(ctx, entity_id, index, &id, &kind, &input_count, &param_count) -> int` | 一条特征 |
| `feature_input_at` | `(ctx, entity_id, feature_id, index, &input_id) -> int` | 依赖边 |
| `feature_param_count` | `(ctx, entity_id, feature_id) -> int` | 参数条数 |
| `feature_param_at` | `(ctx, entity_id, feature_id, index, name_buf, cap, &value) -> int` | 参数（名字缓冲可为空 = 只取值） |
| `begin_transaction` | `(ctx, name) -> int` | 开事务 |
| `commit_transaction` | `(ctx) -> int` | 提交 |
| `abort_transaction` | `(ctx) -> int` | 回滚，返回回滚的命令条数（0 = 空事务） |
| `unregister_plugin` | `(ctx, plugin_id) -> int` | 摘掉一个扩展（命令 + 元数据），返回摘掉几条 |

---

## 3. 点输入

`begin_point_input` 的 `flags` 位：

| 位 | 名字 | 含义 |
|---:|---|---|
| 0 | `kAllowConfirm` | 允许 Enter / 双击提前结束 |
| 1 | `kGridSnap` | 吸附网格 |
| 2 | `kPickEntities` | 报告点落在哪个实体上 |
| 3 | `kEntitiesOnly` | 只接受落在实体上的点（隐含位 2） |

`max_points = 0` 表示不限。`request_id` 由 C# 侧生成，非 0，回调时原样带回。

回调走 `Bootstrap.PointInputCompleted(request_id, points, count, status)`，`points` 是 POD 数组：

```c
struct HostPickPoint {
  float x, y, z;
  std::uint32_t reserved;   // padding，必须保留
  std::uint64_t entity_id;
};
```

---

## 4. 对话框

`show_dialog` 的 `kind`：

| kind | 含义 | 返回 |
|---:|---|---|
| 0 | 消息 | 按钮（见下） |
| 1 | 单行字符串 | |
| 2 | 数字 | |
| 3 | 打开文件 | |
| 4 | 保存文件 | |
| 5 | 多字段表单 | |

返回码：输入类 **0 = 成功、1 = 取消、-1 = 失败**；消息类返回按钮编号 **1 = Ok、2 = Cancel、3 = Yes、4 = No**。

`spec_utf8` 是 `TAG|value` 每行一条的文本（`T` 标题、`L` 标签、`V` 默认值、`MIN`/`MAX` 范围、`FILTER`、`NAME`，表单是 `F|…` 行），值里的 `|` 换成空格、换行换成 `\x1e`。解析见 [`plugin_prompt_spec.cpp`](https://github.com/terry-chao/tamias/blob/main/src/plugin/plugin_prompt_spec.cpp)。

---

## 5. 调用约定与缓冲

| 约定 | 说明 |
|---|---|
| 调用约定 | **cdecl**。C# 委托标了 `CallingConvention.Cdecl`；`Bootstrap.Initialize` / `Invoke` / `PointInputCompleted` / `Evaluate` / `Reload` 是 `[UnmanagedCallersOnly]` |
| 字符串 | UTF-8，以 `'\0'` 结尾 |
| 填缓冲的函数 | 写 `cap - 1` 字节并补 `'\0'`，返回写入长度 |
| 失败 | 查询类返回 `-1`；`dispatch` / `register_command` / `register_plugin` / `set_selection` 成功 0、失败 -1 |
| 线程 | 回调（日志、点输入完成）发生在 **UI 线程** |
| 内存 | 宿主不接管插件分配的内存；缓冲由调用方提供并释放 |

---

## 6. 加一个新 API 要做什么

1. 在 `host_api.h` 的 `HostApi` **末尾**加函数指针，`kHostApiVersion` 加一。
2. 在 `HostApi.cs` 同步加委托类型和 `HostApi` 字段，`HostApiVersion.Current` 加一。
3. 在 `plugin_host.cpp` 填上函数指针的实现（`host_xxx`），不碰已有字段。
4. 在 `Host`（`Tamias.Host`）里包成 C# 友好的成员，补进 `IHost`。
5. 更新本页的版本表与对应的 `docs/plugin/api/*.md`。

不要在 v8 表中间插字段——按旧版本编译的插件会整表错位。
