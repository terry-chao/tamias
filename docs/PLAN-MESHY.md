# Meshy.ai 接入（AI 生成 3D）：方案

> 状态：**M0 / M2 / M3 已落地**（可撤销导入 + 协议层 + Qt 客户端 + **面板与完整交互** +
> 端到端脚本）。**M1（拿真素材量尺度/朝向）与 M4（MCP 工具）还没做**；
> M3 里「解析挪出 UI 线程」一条**有意缓做**，理由见 [§6](#6-分期实施)。
>
> 一句话：**Meshy 出的是美术向三角汤，进 Tamias 只能是「导入网格」**（参考几何 / 占位体），
> 没有 Entity、不进特征树、不可参数化。生成由**人操作的应用内客户端**发起，
> 导入走**一条撤销**，与工具条、脚本、AI 共用同一条命令线。
>
> 相关：[AI 总览](ai/index.md)（门面与写策略）、
> [AI 接入（MCP）](DECISION-AI-INTEGRATION.md)（工具面 / 审计 / 三档策略）、
> [路线图](ROADMAP.md)（格式分工：`.obj/.glb` = 通用 mesh 的导入出口）。

## 1. 为什么这件事比看上去短

接入面**基本都在**，接 Meshy 是拼装而不是开新通道：

| 已有的 | 位置 | 对 Meshy 的意义 |
|---|---|---|
| GLB 导入（含内嵌贴图） | [`mesh_io.cpp`](https://github.com/terry-chao/tamias/blob/main/src/engine/io/mesh_io.cpp) 的 `load_glb_model` | Meshy 默认就出 GLB，格式天然对上 |
| 材质槽位 albedo / normal / orm | [`material.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/render/resource/material.h) | 与 Meshy 的 base_color / normal / roughness+metallic 几乎 1:1（ORM 打包就是 glTF 标准） |
| HTTPS 客户端范式 | [`ai_client.h`](https://github.com/terry-chao/tamias/blob/main/src/app/ai/ai_client.h) | 轮询式任务照抄这一份骨架；`Qt6::Network` 已链进 app |
| 密钥落盘 | [`secret_store.h`](https://github.com/terry-chao/tamias/blob/main/src/app/base/secret_store.h) | Meshy key 加一个槽位，走 Windows 凭据管理器 |
| 缩略图 | [`mesh_thumbnail.cpp`](https://github.com/terry-chao/tamias/blob/main/src/app/shell/widget/mesh_thumbnail.cpp) 的 `render_mesh_thumbnail` | 生成结果出预览图，零新增 |

**不做的三件事**（与 [AI 接入](DECISION-AI-INTEGRATION.md) §1 同源）：

- **不给生成结果开内核后门**：它就是一份三角网，和打开一个 `.glb` 是同一条路。
- **不发明第二套导入路径**：插入文档只有一个入口（`ImportMeshCommand`），
  工具条、脚本、AI、Meshy 客户端都走它。
- **不假装它是参数化构件**：不建 Entity、不塞特征树。想要参数化的墙/柱，走
  [图纸 → BIM](DRAWING-TO-BIM.md) 那条线。

## 2. Meshy 侧的交互形状

Meshy 是**异步任务式 REST**，不是同步推理：建任务 → 轮询 → 拿 `model_urls.glb` 下载。

```
POST   建任务（text-to-3d 的 preview / refine，或 image-to-3d / text-to-texture）
  → 返回 task id
GET    轮询任务：status（PENDING / IN_PROGRESS / SUCCEEDED / FAILED / CANCELED）+ progress
  → SUCCEEDED 时给 model_urls（glb / fbx / obj / usdz…）+ thumbnail + 分通道贴图 url
GET    下载 GLB
```

三个定性结论（与具体版本无关）：

1. **Auth 是 Bearer 密钥**，与 LLM 的 key 不是一回事，要单独一个槽位。
2. **单次生成是分钟级**：必须有进度、能取消、能看见花了多少额度。
3. **输出格式要挑**：选 GLB。Tamias 只吃 `.obj` / `.glb`，FBX / USDZ 进来是死路。

> ⚠️ **待核实**：本文写作环境访问不到 `docs.meshy.ai` / `api.meshy.ai`（连接超时）。
> 端点路径、模型名、preview/refine 的计费与轮询建议间隔、额度查询端点，**以官方文档为准**，
> 动手前先对一遍。

## 3. 三条路线，选应用内客户端

| | **A 应用内 Meshy 客户端（选定）** | B MCP 工具 | C C# 插件 / 扩展 |
|---|---|---|---|
| 谁发起生成 | Tamias 自己 | 外部 Agent（Claude / Cursor / Codex） | 插件自己 |
| 传输 | `QNetworkAccessManager` | stdio → 桥 → 门面 | .NET `HttpClient`（全信任） |
| 受 MCP 写策略约束 | **不受**（人在自己点，与命令控制台同性质） | 受 `--mcp-policy` | 不受 |
| 还缺什么 | Meshy 客户端 + 面板 | `tamias_generate_3d` / `tamias_import_mesh` 两个工具 | ABI 入几何原语 + 打包 .NET |
| 改动面 | app 层 + 一个命令 | 门面 + 命令 | C ABI（最贵） |

选 A 的理由：密钥与网络只出现在一个地方（应用进程），生成是**人点的操作**，
因此天然绕开"MCP 写策略该不该放行联网生成"这道更麻烦的题；而且它复用
[`AiClient`](https://github.com/terry-chao/tamias/blob/main/src/app/ai/ai_client.h) 已经验证过的 Qt 网络骨架。

B 是**后续**的加法（Agent 端到端生成 → 插入），不是前提；C 只在需要"零 C++ 改动做原型"时才值得。

## 4. 与内核的边界：三角汤就是三角汤

Meshy 的输出是**美术向**网格：几万个三角、PBR 贴图、没有拓扑语义、没有参数。
它在 Tamias 里的正确身份是 [§1](#1-为什么这件事比看上去短) 说的**导入网格**：

| | 导入网格（Meshy 结果） | 参数化构件（墙 / 柱 / 板） |
|---|---|---|
| 载体 | `MeshAsset` + `SceneNode` | `Entity` + 特征树 |
| 改尺寸 | 只能整体缩放 / 移动 | 改参数 → 重算 |
| 存进 `.tdoc` | 三角网 | 特征树（三角网是缓存） |
| 用途 | 参考几何、占位体、概念体量 | 交付、算量、出图 |

**这条边界要写在 UI 上**，否则用户会期待"生成一件能改厚度参数的墙"，然后失望。

## 5. 已落地 / 还缺什么

**已落地：**

| 能力 | 落点 |
|---|---|
| 可撤销的网格导入（几何 + 贴图 + 材质 + 场景节点 = 一条撤销） | [`import_mesh_command.h`](https://github.com/terry-chao/tamias/blob/main/src/command/import/import_mesh_command.h) |
| 可分发命令 `import_mesh`（路径 + 名称 + 放置 + 缩放） | [`register_commands.cpp`](https://github.com/terry-chao/tamias/blob/main/src/command/core/register_commands.cpp) |
| app 侧入口：读盘 + **用 QImage 解码内嵌贴图** + 一条撤销 | `DocumentViewport::import_mesh_file` |
| `Document::remove_material`（撤销要连带收掉新建材质） | [`document.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.h) |
| 单测：撤销 / 重做 / 贴图槽位 / 去重 / 失败路径 / 分发放置 | [`import_mesh_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/import_mesh_tests.cpp) |
| **协议层**：建任务请求 / 回包解析 / 状态机判定 / 端点拼接（纯 C++，无 Qt） | [`meshy_protocol.h`](https://github.com/terry-chao/tamias/blob/main/src/ai/meshy_protocol.h)、单测 [`meshy_protocol_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/meshy_protocol_tests.cpp) |
| **Qt 客户端**：建任务 → 轮询 → 下载到本地文件；可取消、可查额度 | [`meshy_client.h`](https://github.com/terry-chao/tamias/blob/main/src/app/ai/meshy_client.h) |
| 设置项与密钥：`meshy/base_url`、`meshy/model`、凭据槽位 `meshy/api-key` | [`app_settings.h`](https://github.com/terry-chao/tamias/blob/main/src/app/base/app_settings.h) |
| 调试入口 `--meshy-prompt="…"`：生成一个模型并导入当前文档 | [`main_window.cpp`](https://github.com/terry-chao/tamias/blob/main/src/app/shell/main_window.cpp) |
| **面板**：提示词 / 风格 / 目标面数 / 进度条 / 阶段 / 取消 / **细化**（用 `preview_task_id` 出精模）/ 额度查询 / 设置对话框（服务地址、模型、API 密钥进凭据管理器） | [`meshy_panel.h`](https://github.com/terry-chao/tamias/blob/main/src/app/shell/panel/meshy_panel.h) |
| 面板停靠与入口：视图 → 面板 → AI 生成 3D，`Ctrl+Shift+M` | [`main_window.cpp`](https://github.com/terry-chao/tamias/blob/main/src/app/shell/main_window.cpp) |
| 端到端脚本：假 Meshy 服务端 + 真 tamias.exe | [`scripts/check-meshy.ps1`](https://github.com/terry-chao/tamias/blob/main/scripts/check-meshy.ps1)、[`scripts/fake-meshy.py`](https://github.com/terry-chao/tamias/blob/main/scripts/fake-meshy.py) |
| 密钥写入脚本（面板之外的手工路径，CI / 无界面场景用） | [`scripts/set-meshy-key.ps1`](https://github.com/terry-chao/tamias/blob/main/scripts/set-meshy-key.ps1) |

**还缺：**

| # | 缺口 | 不做的后果 | 落点 |
|---|---|---|---|
| 1 | 单位与朝向实测（真素材） | 模型缩放错、躺倒 | 先拿一个真实 GLB 对（见 [§7](#7-四个真实的坑)） |
| 2 | MCP 工具（可选，B 路线） | Agent 不能端到端生成并插入 | [`session_mcp_backend.cpp`](https://github.com/terry-chao/tamias/blob/main/src/host/session_mcp_backend.cpp) |
| 3 | 解析挪出 UI 线程 | 大模型导入时界面会卡一下（量级见 [§6](#6-分期实施)） | `DocumentViewport::import_mesh_file` |
| 4 | 缩略图 | 不做也不缺：生成结果直接进视口，人本来就看得见 | — |

## 6. 分期实施

| 阶段 | 内容 | 验收 |
|---|---|---|
| **M0** ✅ | 可撤销导入（命令 + app 入口 + 单测） | 导入一个 GLB，`Ctrl+Z` 一次全退（节点 / 网格 / 材质 / 贴图） |
| **M1** | 真实素材对账：拿 Meshy 导出的 GLB 走 `import_mesh_file`，量尺度 / 朝向 / 贴图 | 尺寸与朝向正确，或明确记下要补的换算 |
| **M2** ✅ | `meshy_protocol` + `meshy_client`（建任务 → 轮询 → 下载）+ 设置项 + `--meshy-prompt` | 一句提示词产出 GLB 并作为导入网格进当前文档（端到端脚本已跑通） |
| **M3** ✅ | 面板 UI：提示词 / 风格 / 面数 / 进度 / 阶段 / 取消 / 额度 / 设置（密钥进凭据管理器）+ refine | 面板里一句提示词能产出并导入；生成期间界面不冻（下载是流式的、轮询是定时器）；能中途取消 |
| **M3 遗留** | 解析挪出 UI 线程 | **故意缓做**：唯一会阻塞的是「下载完之后解析文件 + 解码贴图」这一步，量级在几百毫秒（无节点变换、单 primitive 的文件更快），而把 `ImportedModel` 递回 UI 线程要引入跨线程投递。**先按 M1 拿真素材量一次**，超过 0.5 秒再连加载器一起改，别为猜出来的耗时加复杂度 |
| **M4**（可选） | MCP 工具 `tamias_generate_3d` + `tamias_import_mesh`（写工具，受 `ask` 策略） | 外部 Agent 能生成并插入，且每次调用在命令控制台留痕 |

**M1 必须排在 M2 前面**：加载器的真实能力只有拿真素材才能定，先写网络代码会把
"解析不了"误判成"接口不对"。

## 7. 四个真实的坑

1. **GLB 加载器是手写的最小子集。** [`load_glb_model`](https://github.com/terry-chao/tamias/blob/main/src/engine/io/mesh_io.cpp) 用字符串查找 +
   `strtod` 解析 JSON：**只取第一个 mesh primitive**、**完全忽略 `nodes` 的变换**、
   忽略 `accessor.byteStride`、假定 POSITION / TEXCOORD_0 是 float32 紧凑排列。
   Meshy 的 GLB 如果用了节点级缩放或旋转（导出器做单位 / 坐标转换的常见做法），
   导入会**缩放或朝向出错**；拆成多个 primitive 时只会进来一部分。
   → M1 的第一件事就是拿真素材验这个；要支持就得升级加载器（见 [§6](#6-分期实施)）。
2. **贴图解码在 app 层。** 引擎只存 RGBA8（[`TextureAsset`](https://github.com/terry-chao/tamias/blob/main/src/engine/render/resource/texture_asset.h)），
   解码靠 `QImage`。所以**命令层的 `import_mesh` 只有几何**；带贴图要走
   `DocumentViewport::import_mesh_file`。这不是瑕疵，是分层：`src/command` 不带 Qt。
3. **长任务。** 轮询用 `QNetworkAccessManager` 是事件驱动的，不阻塞；
   但**下载几十 MB 再解析 GLB 会卡住 UI 线程**（`src/app` 里目前没有 `QThread` / `QtConcurrent`）。
   这与 [AI 接入 §5.2](DECISION-AI-INTEGRATION.md) 第 9b 条是同一个坑。
4. **信任面。** `import_mesh` 能吃任意本地路径，等于让 AI 有能力"读任意文件并要求它是网格"。
   走 B 路线时先补一条纪律：MCP 侧要么只接受**应用自己下载**的 URL / 受控临时路径，
   要么把这条命令列进文件类黑名单。今天代码里**还没有**命令黑名单，
   [AI 接入](DECISION-AI-INTEGRATION.md) §7 那条"默认禁掉文件导入导出"仍只是纪律。
5. **显卡驱动会假装成"Meshy 的 bug"。** 走端到端脚本时遇到过一次 `tamias.exe` 以
   `0xC0000005` 退出。对照实验说明它**与 Meshy 无关**：把 ini 的 `render/backend`
   改成 `OpenGL`、**不带任何 Meshy 参数**纯启动（只开 `assets/samples/cube.obj`），
   同样 5 秒内以同一个退出码崩掉；而"上一次进程不是正常退出（脚本里 `Stop-Process`
   收尾就会这样）"正好会把下一次启动降级到 OpenGL，于是它披着 Meshy 的外衣出现。
   根因与修法见 [RHI 卡死 / 崩溃定位](RHI-TRIAGE.md)，本机那块驱动已由
   [`rhi_blocklist.json`](https://github.com/terry-chao/tamias/blob/main/assets/rhi_blocklist.json)
   登记为 `force: vulkan`。
   [`check-meshy.ps1`](https://github.com/terry-chao/tamias/blob/main/scripts/check-meshy.ps1)
   跑之前仍会**显式**把后端钉成 Vulkan：验证要考的是 Meshy，不该受任何一层的后端选择影响。

## 8. 安全与成本

- **密钥**：走 [`SecretStore`](https://github.com/terry-chao/tamias/blob/main/src/app/base/secret_store.h)（Windows 凭据管理器，DPAPI 按用户加密），
  不进 `.tdoc`、不进 ini、不进日志。
- **额度**：生成是**付费**操作。面板必须在发起前显示预估消耗、发起后显示进度，
  并且**默认不自动重试**——失败自动重刷额度是用户最不想看到的意外。
- **上传内容**：image-to-3d 会把图片发给第三方。这条要在 UI 里说清楚，
  与"内部项目不外发"的团队预期冲突时要能一眼看出。

## 9. 验收

| 层 | 指标 | 门槛 |
|---|---|---|
| 导入 | 一次生成 = 一条撤销；撤销后文档回到导入前（节点 / 网格 / 材质 / 贴图都不留） | 单测已覆盖 |
| 导入 | 同一份 GLB 导两次，网格资产只有一份 | 单测已覆盖 |
| 协议 | 不认识的状态不当完成、缺 status 算失败、进度夹到 0–100、refine 不带造型参数 | 单测已覆盖（13 例） |
| 回路 | 提示词 → 建任务（带 Bearer）→ 两轮轮询 → 下载 → 导入成节点 | `scripts/check-meshy.ps1` 已跑通 |
| 尺度 | 真素材导入后的包围盒与 Meshy 页面显示一致 | M1 实测 |
| 交互 | 生成期间界面不冻结，可取消 | M3 |
| 审计 | 每次生成 / 导入在命令控制台留痕（走 B 路线时） | M4 |

### 9.1 怎么跑那条端到端

```powershell
pwsh -NoProfile -File scripts\check-meshy.ps1
```

它用 `scripts/fake-meshy.py` 起一个假服务端（真的会吐一个能被 loader 读进去的最小 GLB），
临时把 `[meshy] base_url` 指过去、临时往凭据管理器写一个测试密钥，跑完后**两处都还原**。
断言是：建任务带了 Bearer、轮询走满、下载被请求、应用日志出现
`Meshy: imported … as node …`（只有导入真的落地才会打这一行）。

> ⚠️ 写测试密钥**别用 `cmdkey`**：它把口令按 UTF-16 塞进 `CredentialBlob`，而
> [`secret_store.cpp`](https://github.com/terry-chao/tamias/blob/main/src/app/base/secret_store.cpp)
> 按 UTF-8 读，读出来是 NUL 交错的乱码——表现是请求头里的 key 静默变成一串垃圾、
> 服务端回 401，而应用侧看起来"已经配置好了"。脚本因此用 CredWriteW 自己写 UTF-8 blob。

## 10. 与现有文档的关系

- [AI 接入（MCP）](DECISION-AI-INTEGRATION.md)：本文的 M4 是它工具面的加法；
  写操作仍然只有 `dispatch` 一条路，导入命令照样一调用一事务。
- [路线图](ROADMAP.md)：`.obj/.glb` 本来就是"通用 mesh 的导入出口"，本文只是给它接了个生产者。
- [图纸识别（RF-DETR）](PLAN-VISION.md)：两条线都在"给内核喂外部世界的东西"，
  区别是那条线喂**语义证据**、这条线喂**几何本身**；两条都不碰几何内核。
- [RAG 实施计划](PLAN-RAG.md)：同一套纪律——**纯 C++ 模块 + Python / 网络只做边缘**。
  本文的例外是"M2 起 app 会联网"，所以密钥与开关要能关掉。
