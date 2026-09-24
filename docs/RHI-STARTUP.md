# RHI 启动：探测、降级、块名单与安全模式

> 状态：**已落地**。回答一个问题：**这台机器上该用哪个图形后端，怎么决定，坏了怎么办。**
>
> 相关：[管线与 RHI](RENDERING.md)（§5 讲后端抽象）、[构建](BUILD.md)。

---

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
         → 上次可用的后端（干净退出的前提下）
            → 默认顺序：Vulkan → OpenGL
```

块名单**不在这条链里**——它按「GPU 身份」匹配，而身份要建出设备才知道，所以在探测**过程中**
逐后端生效（见 §3）。规则落在 `decide_rhi_startup()`（纯函数、可单测）。

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
  Vulkan 后端按厂商拆包（见 `format_vulkan_driver_version()`）；OpenGL 目前拿不到驱动版本，
  所以点名驱动版本的条目在 GL 上不会命中。
- 一台机器报不出驱动版本时，点名了版本的条目**宁可不命中**。
- 坏条目（认不出的 `skip` 值、没有动作、`os` 拼错）只跳过那一行并记 warning，**不影响其它条目**。
- 命中会写进日志与体检报告：
  `RHI: using OpenGL; Vulkan -> blocked by blocklist entry "intel-24x-rdp-swapchain"`。

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

1. 开窗口**之前**写标记文件（`%APPDATA%\tamias\tamias\startup.marker`）；
2. 界面起来后 1.5 秒清掉；
3. 下次启动时标记还在 → **上次死在启动期** → 强制 OpenGL + 关校验层，并记一条 warning。

代价是一次启动多一个文件读写；收益是「坏驱动把进程搞崩」不会变成「每次开机都崩」——
用户至少还能用 OpenGL 打开软件，把日志交给 IT。

## 6. 命令行

| 命令 | 干什么 |
|---|---|
| `tamias --probe-rhi` | 无窗口体检：跑 B 档探测，打印一行结论；退出码 `0` = 有可用后端，`2` = 没有 |
| `tamias --probe-rhi --json` | 体检报告输出 JSON（适配器名 / 驱动版本 / API 版本 / 失败原因 / 命中条目） |
| `tamias --gpu-backend=opengl` | 只试这个后端（调试用；策略优先于它） |
| `tamias --safe-mode` | 只试 OpenGL、关校验层（驱动出问题时的逃生口） |

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
[warn] 上次启动没走完（可能在建设备/首帧崩了），本次强制安全模式   ← 触发安全模式时
[warn] 回退到 OpenGL（用户偏好是 Vulkan）           ← 降级时
```

**用户偏好不被降级结果覆盖**：`render/backend` 始终是用户选的，探测结果只作用于本次会话
（`resolved_backend()`）；另外单独记 `render/last_good_backend` + `render/last_good_gpu`（GPU 指纹）。
驱动升级 / 换卡后指纹变了，日志会说一声。

## 8. 现在刻意没做的

- **feature 级 workaround**（只关某个扩展，比如 calibrated timestamps）：等真遇到再加；
  现在只有「跳过后端 / 改走后端」两种动作，不先加空壳。
- **块名单远程更新**：文件随包发；要远程更新，换掉 `load_rhi_blocklist()` 的读取来源即可。
- **图形诊断面板**（把报告显示出来、一键复制给 IT）：报告已经结构化，接 UI 是下一步。
- **OpenGL 的驱动版本**：Windows 上要读注册表才拿得到（Qt 也这么干），未做。
- **多设备**：volk 的函数表是进程全局的，当前只允许一台 Vulkan 设备（见 [RENDERING.md](RENDERING.md) §5）。
- **离屏渲染的消费者**：RHI 层已经能离屏出图（缩略图 / 截图 / 像素级金样都够用了），但还没接具体
  功能——下一步是「当前视图导出 PNG」和 `RenderSceneGolden` 的像素比对。

## 9. 测试

`tests/rhi_startup_tests.cpp`（15 条，**全都不需要 GPU**）：

- 版本比较：`24.9 < 24.20`、缺段当 0、带尾巴
- 块名单：厂商 / 设备 / 版本区间 / 远程 / 软件渲染 / 名字子串；第一条命中即停；
  坏行只跳过自己；`skip` 与 `force` 解析；策略解析
- 决策链：安全模式 > 策略 > 命令行 > 上次可用 > 默认，以及「上次没干净退出就不信任它」
- 探测（**注入假设备工厂**）：首个后端失败 → 降级成功；全失败 → 无可用后端；
  B 档抓「建得出但提交失败」；块名单跳过 / 改道；策略 override
- GPU 指纹随驱动 / 设备变化

`tests/vulkan_loader_tests.cpp` 是真机冒烟（有 runtime 才跑）。

**手工验证**：`tamias --probe-rhi --json`；往 `assets/rhi_blocklist.json` 里加一条
`{ "id": "check", "vendor": 4318, "skip": "vulkan" }`，再看报告里 Vulkan 是否被跳过、
是否落到 OpenGL。
