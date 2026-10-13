# RHI 启动：探测、降级、块名单与安全模式

> 状态：**已落地**。回答一个问题：**这台机器上该用哪个图形后端，怎么决定，坏了怎么办。**
>
> 相关：[管线与 RHI](RENDERING.md)（§5 讲后端抽象）、[构建](https://github.com/terry-chao/tamias/blob/main/BUILD.md)。

---

## 0. 代码在哪：`tamias::rhi_selection` 组件

这套选择逻辑已经抽成一个**与宿主无关的组件**，放在 [`src/rhi_selection/`](../src/rhi_selection/README.md)，
CMake 目标 `tamias::rhi_selection`。它只依赖标准库（加一个 12 行的 `result.h`），不认识 Qt、
不认识具体渲染 API，所以**别的软件也能直接拿去用**。

| 层 | 位置 | 职责 |
|---|---|---|
| 组件：决策 | `src/rhi_selection/decision.*` | `decide_rhi_startup()`（纯函数）：排候选顺序 |
| 组件：探测 | `src/rhi_selection/probe.*` | `probe_rhi()`：按序试、降级、块名单在此生效 |
| 组件：数据 | `src/rhi_selection/{identity,blocklist,os,backend}.*` | GPU 身份 / 块名单 / 策略 / 后端枚举 |
| 组件：入口 | `src/rhi_selection/startup.*` | `run_rhi_startup()`：决策 + 探测一步到位 |
| 组件：接入点 | `src/rhi_selection/device.h` | `IBackendDevice` / `BackendDeviceFactory` |
| tamias 适配 | `src/engine/render/rhi/rhi_selection_device.*` | 把 `RHIDevice` 适配成 `IBackendDevice` |
| 宿主胶水 | `src/app/base/rhi_cli.*`、`startup_guard.*`、`main.cpp` | 读命令行 / 策略 / 设置 / 标记，写回记忆 |

组件怎么被别的程序接入，见 [`src/rhi_selection/README.md`](../src/rhi_selection/README.md)。
下面正文里说的 `decide_rhi_startup()` / `probe_rhi()` 都在这个组件里。

## 0. 先分清三类失败

| 失败 | 例子 | 进程内探测能不能兜住 |
|---|---|---|
| 缺 runtime | 没装 `vulkan-1.dll` / `libvulkan1` | 能，而且极便宜（volk 那一步） |
| 没有合适设备 | 只有基础显示适配器、GPU 被策略禁用、纯 RDP、显存不足 | 能，枚举设备即可 |
| 驱动坏了 | 建得出设备但一提交就崩 / TDR / 卡死 | **不能**，探测本身会把进程搞死 —— 靠**安全模式**兜 |

前两类占日常故障的绝大多数，所以**进程内探测 + 自动降级**就够了；第三类要的是「记住上次死过」，
不是「另起一个探测进程」。独立探测进程只在两种情况下才值得：探测本身危险到要隔离，
或者必须在建窗口之前就定下图形栈。tamias 都不属于这两种。

## 1. 决策顺序

```
--safe-mode（最高）
   → IT 策略 force_backend（可 lock）
      → --gpu-backend=X
         → 上次死在启动期（启动标记还在）→ 安全模式
            → 用户偏好（设置里选的）
               → 上次可用的后端（干净退出的前提下，作为第二顺位）
                  → 另一个后端兜底
```

块名单**不在这条链里**——它按「GPU 身份」匹配，而身份要建出设备才知道，所以在探测**过程中**
逐后端生效（见 §3）。规则落在 `decide_rhi_startup()`（纯函数、可单测）。

**命令行排在启动标记之前是刻意的**：标记是自动判断，命令行 / 策略是人在现场给的证据。
反过来的话，标记一留下连 `--gpu-backend=vulkan` 都救不回来（2026-10-13 那台机器就是这么卡死的，
见 [RHI 卡死 / 崩溃定位](RHI-TRIAGE.md) §7.1）。

**安全模式不是「一律退回 OpenGL」**，而是「避开标记里那个后端」——候选顺序按上面这条链正常排，
只把标记里记着的那个后端挪到最后（见 §5）。

**只有「自然选中并跑通」的会话才会写进记忆**：安全模式、策略强制、`--gpu-backend` 指定这三种
不算（`remember_result`）。否则一次崩溃循环（被迫走 OpenGL）就会把这台机器永久降级。

## 2. 探测：两档深度

| 档 | 做什么 | 用在哪 | 代价 |
|---|---|---|---|
| **A：只建设备** | 建 instance / 选物理设备 / 建 device / VMA 分配一小块显存 | **正常启动** | 几十毫秒 |
| **B：再空提交** | 建一次性 command buffer、空录制、`vkQueueSubmit` + fence（GL 侧 `glFinish`） | 需要 B 档时（自检脚本） | +几毫秒 |
| **C：离屏画一个像素** | 建离屏目标 → 画 1×1 纯色 → 读回比对 | **`--probe-rhi`** ✅ | +几毫秒（含一次真实提交） |

「建得出 ≠ 能提交」是最阴的一类故障（坏驱动、TDR 之后的设备、远程会话下的提交路径），
所以体检档一路验到「真的能出图」——C 档天然包含 B 档（读回之前必须提交一次完整帧）。
为此在 RHI 上加了三个口子（都有默认实现，后端按需覆盖）：

| 口子 | 干什么 |
|---|---|
| `RHIDevice::gpu_identity()` | 厂商 / 设备号 / 驱动版本 / API 版本 —— 块名单的输入 |
| `RHIDevice::submit_noop()` | 提交一次空命令并等完成（Vulkan 5 秒超时，挂住就换下一个后端） |
| `RHIDevice::create_offscreen_swap_chain(w,h)` + `SwapChain::read_back_rgba()` | 无窗口渲染 + 像素读回（RGBA8、左上原点） |

## 3. 块名单（blocklist）

「哪块卡 + 哪个驱动版本 + 哪个环境 → 会坏 → 怎么绕」，**写成数据而不是代码**：
发现新的坏驱动改一行 JSON 即可，不用重新编译发版（浏览器就是这么干的）。

文件：`assets/rhi_blocklist.json`（随包）。格式是 **JSON 的扁平子集**，一个条目一个对象，
跨行写也行：

```json
{
  "version": 1,
  "entries": [
    { "id": "intel-24x-rdp-swapchain",
      "why": "Intel 24.20–24.29 在 RDP 会话里建 Vulkan swapchain 会崩",
      "os": "windows", "vendor": 32902, "remote": true,
      "driver_min": "24.20.0.0", "driver_max": "24.29.9.9",
      "skip": "vulkan" },
    { "id": "nvidia-old-driver",
      "vendor": 4318, "driver_exact": "566.0.3.0",
      "force": "opengl" }
  ]
}
```

| 字段 | 含义 | 不填 |
|---|---|---|
| `id` / `why` | 稳定标识 + 人读原因（日志和工单里引用） | `id` 必填 |
| `os` | `windows` / `linux` / `mac` | 不限 |
| `vendor` / `device` | PCI 号（NVIDIA 4318、Intel 32902、AMD 4098） | 不限 |
| `driver_min` / `driver_max` / `driver_exact` | 驱动版本区间（数值分段比较，`24.9 < 24.20`） | 不限 |
| `remote` / `software` | 是否远程会话 / 是否软件渲染（`true` / `false`） | 不限 |
| `adapter_contains` | 适配器名子串（大小写无关），例如 `llvmpipe` | 不限 |
| `skip` / `force` | 动作：跳过这个后端 / 改走另一个后端（`vulkan` / `opengl`） | 二者必居其一 |

约定与容错：

- **驱动版本写成四段数字**（NVIDIA 566.03 → `566.0.3.0`）。各家 `driverVersion` 打包方式不同，
  Vulkan 后端按厂商拆包（见 `format_vulkan_driver_version()`）。
- **两个后端的驱动版本号写法不一样，条目按平台分开写**：Vulkan 是「厂商拆包形式」
  （NVIDIA 566.03 → `566.0.3.0`）；OpenGL 在 Windows 上读注册表的 `DriverVersion`，那是
  **Windows 驱动版本号**（NVIDIA 566.36 在这里形如 `32.0.15.9636`）。数值分段比较不会让两者
  误命中，所以混写只会「不匹配」，不会误伤。
- 承上：**同一条驱动，两个后端报的是两个字符串**——610.88 这条驱动在 Vulkan 侧是
  `610.88.0.0`、在 OpenGL 侧是 `32.0.16.1088`（本机实测）。条目要拦哪个后端，就用那个后端
  的写法；写成另一个不会误伤，但**也不会命中**。
- **拦 OpenGL 的条目不能填 `device`**：OpenGL 侧拿不到 device_id（恒 0，见 `identity.h`），
  填了就永远不会命中。OpenGL 侧能用的字段是 `os` / `vendor` / 驱动版本 / `adapter_contains`。
- 一台机器报不出驱动版本时，点名了版本的条目**宁可不命中**。
- 坏条目（认不出的 `skip` 值、没有动作、`os` 拼错）只跳过那一行并记 warning，**不影响其它条目**。
- 命中会写进日志与体检报告：
  `RHI: using OpenGL; Vulkan -> blocked by blocklist entry "intel-24x-rdp-swapchain"`。

> **条目会过期，过期就该删。** 块名单挡的是「我们暂时绕不过去的坏组合」；一旦从我们这边
> 修掉了，条目留着只会白白挡住本来能用的后端。2026-10-13 就有一条：NVIDIA 610.88 + OpenGL
> 曾按「一画帧就崩」加了 `force: vulkan`，当天查明真正原因是**我们 GL 变体的 SPIR-V 不合
> ARB_gl_spirv**（[RHI 卡死 / 崩溃定位](RHI-TRIAGE.md) §10），修完即删。删条目和加条目一样，
> 都要在提交信息里写清「为什么」。

## 4. 策略（IT 下发）

块名单是「我们（厂商）知道它坏」；策略是「你们（客户）要求这么用」。文件放：

```
%PROGRAMDATA%\tamias\rhi_policy.json      ← IT 统一下发
<exe 旁边>\rhi_policy.json                ← 便携部署 / 调试
```

```json
{ "force_backend": "opengl", "lock": true, "override_blocklist": false }
```

| 字段 | 含义 |
|---|---|
| `force_backend` | 一律用这个后端（优先级仅次于 `--safe-mode`） |
| `lock` | 锁住：界面里不让用户改 |
| `override_blocklist` | 显式要求压过块名单。**默认 false**——命中已知坏驱动时防崩优先；真要压，日志里会留着记录 |

## 5. 安全模式

挡「建得出设备但一提交就崩」这类问题：

1. 开窗口**之前**写标记文件（`%APPDATA%\tamias\tamias\startup.marker`），里面记着**这次用的是
   哪个后端**；
2. 界面起来后 1.5 秒清掉；
3. 下次启动时标记还在 → **上次死在启动期** → 走安全模式，并记一条 warning。

代价是一次启动多一个文件读写；收益是「坏驱动把进程搞崩」不会变成「每次开机都崩」——
用户至少还能开起来，把日志交给 IT。

安全模式**不是**「一律退回 OpenGL」，而是「**避开标记里那个后端**」：

- 候选顺序照常（偏好 → 上次可用 → 另一个），只把标记里记着的那个后端**排到最后**；
- 关校验层（校验层自己也要加载驱动代码）；
- 本次结果**不写记忆**（`remember_result = false`），免得一次被迫的降级变成永久设置。

块名单在安全模式里**照样生效**：所以「已知坏驱动」既不会被排到前面，也不会在最后兜底时
把进程带崩。

> **注意：这条兜底的前提是「安全模式那条路本身是好的」。** 2026-10 的一次现场正好相反：
> 那台机器的 OpenGL 驱动自己会挂/崩，于是变成「GL 崩 → 留下标记 → 下次强制 GL → 又崩」的
> 自锁循环；而且**标记在决策顺序里排在命令行之前**，`--gpu-backend=vulkan` 也压不过它
> （实测日志仍是 `Creating RHI device: OpenGL`，随后卡死）。另外 `--render-view` 这类 CLI
> 出图也会写这个标记（`startup_guard.begin()` 在 CLI 分支之前调用），一次出图就会把下次 GUI
> 启动推进安全模式。**这三处都已在 2026-10-13 改掉**（决策顺序、标记内容、CLI 不写标记），
> 完整证据链见 [RHI 卡死 / 崩溃定位](RHI-TRIAGE.md) §7 / §9。

## 6. 命令行

| 命令 | 干什么 |
|---|---|
| `tamias --probe-rhi` | 无窗口体检：跑 C 档探测（离屏画一个像素并读回），打印一行结论；退出码 `0` = 有可用后端，`2` = 没有 |
| `tamias --probe-rhi --json` | 体检报告输出 JSON（适配器名 / 驱动版本 / API 版本 / 失败原因 / 命中条目） |
| `tamias --gpu-backend=opengl` | 只试这个后端（调试用；策略优先于它，但它压过启动标记） |
| `tamias --safe-mode` | 安全模式：关校验层、避开启动标记里那个后端、不写记忆（驱动出问题时的逃生口） |
| `tamias --diagnostics-report=out.txt` | 把「图形诊断」面板显示的那份文本写成文件后退出（IT / 脚本收集现场用） |

**应用内入口：帮助 → Graphics Diagnostics**（`Ctrl` 快捷键无）——同一个对话框里能看到：
本次实际后端与用户偏好、是否降级 / 安全模式、策略与块名单状态、探测报告（适配器 / 驱动版本 /
API 版本 / 每条尝试的原因）、以及**最近 400 行日志**；底下两个按钮：**Copy to Clipboard**
（客户把这段贴进工单即可）和 **Save Report…**。

面板**不重新探测**：volk 是单设备模型，进程里已有渲染设备时再建一台 Vulkan 设备会被守卫拒绝，
所以展示的是启动那一刻的可信快照（`RhiDiagnostics` 缓存）。报告文本与
`--diagnostics-report` 输出**同一份**（`build_diagnostics_report()`），不会两边不一致。

体检报告例子（本机实测）：

```json
{ "chosen": "Vulkan",
  "summary": "RHI: using Vulkan",
  "attempts": [
    { "backend": "Vulkan", "ok": true, "reason": "ready", "matched_entry": "",
      "adapter": "NVIDIA GeForce RTX 5070", "driver_version": "596.49.0.0",
      "vendor_id": 4318, "device_id": 12036, "api_version": 4211017,
      "remote": false, "software": false } ] }
```

## 7. 启动日志里能看到什么

```
[info] RHI startup: last known good: Vulkan        ← 谁做的决定
[info] RHI: using Vulkan                           ← 结果（失败时带上每个后端的原因）
[warn] 上次启动没走完（可能在建设备/首帧崩了），本次走安全模式   ← 触发安全模式时
[warn] 回退到 OpenGL（用户偏好是 Vulkan）           ← 降级时
```

**用户偏好不被降级结果覆盖**：`render/backend` 始终是用户选的，探测结果只作用于本次会话
（`resolved_backend()`）；另外单独记 `render/last_good_backend` + `render/last_good_gpu`（GPU 指纹）。
驱动升级 / 换卡后指纹变了，日志会说一声。

## 8. 现在刻意没做的

- **feature 级 workaround**（只关某个扩展，比如 calibrated timestamps）：等真遇到再加；
  现在只有「跳过后端 / 改走后端」两种动作，不先加空壳。
- **块名单远程更新**：文件随包发；要远程更新，换掉 `load_rhi_blocklist()` 的读取来源即可。
- **面板里的「重新检测」**：单设备模型下没法在运行中重新建 Vulkan 设备（要 `volkLoadDeviceTable`
  支持多设备才行）；面板现在显示的是启动快照。
- **OpenGL 的驱动版本在 Linux 上**：Windows 已从注册表拿到（见 §3 的写法差异）；Linux 要从
  Mesa 的 `GLX_MESA_query_renderer` 之类取，未做。
- **多设备**：volk 的函数表是进程全局的，当前只允许一台 Vulkan 设备（见 [RENDERING.md](RENDERING.md) §5）。
- **离屏渲染的消费者**：RHI 层已经能离屏出图（缩略图 / 截图 / 像素级金样都够用了），但还没接具体
  功能——下一步是「当前视图导出 PNG」和 `RenderSceneGolden` 的像素比对。
- ~~**安全模式的兜底后端**：现在硬编码 OpenGL~~ —— 2026-10-13 已改成「按自然顺序 + 把标记里
  那个后端排到最后」（见 §5）。

## 9. 测试

`tests/rhi_selection_tests.cpp`（18 条，**全都不需要 GPU**）：

- 版本比较：`24.9 < 24.20`、缺段当 0、带尾巴
- 块名单：厂商 / 设备 / 版本区间 / 远程 / 软件渲染 / 名字子串；第一条命中即停；
  坏行只跳过自己；`skip` 与 `force` 解析；策略解析
- 决策链：`--safe-mode` > 策略 > 命令行 > 启动标记 > 偏好 / 上次可用 > 默认；
  「上次没干净退出就不信任它」；安全模式把标记里那个后端排到最后；命令行压过标记
- 探测（**注入假设备工厂**）：首个后端失败 → 降级成功；全失败 → 无可用后端；
  B 档抓「建得出但提交失败」；块名单跳过 / 改道；策略 override
- GPU 指纹随驱动 / 设备变化

`tests/vulkan_loader_tests.cpp` 是真机冒烟（有 runtime 才跑）。

**手工验证**：`tamias --probe-rhi --json`；往 `assets/rhi_blocklist.json` 里加一条
`{ "id": "check", "vendor": 4318, "skip": "vulkan" }`，再看报告里 Vulkan 是否被跳过、
是否落到 OpenGL。
