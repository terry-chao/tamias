# rhi_startup —— 图形后端启动选择组件

一个**与宿主无关**的小组件，只干一件事：**这台机器上该用哪个图形后端，怎么决定，
坏了怎么办。** 它不参与任何实际渲染——只负责「选」，选完把后端名字交回去。

抽出来是为了让别的程序也能复用（不限于 tamias）。同样的需求到处都有：Vulkan 与 OpenGL
之间降级、按显卡 / 驱动版本屏蔽已知坏驱动、记住上次能用的后端、崩溃后进安全模式。

## 边界

组件**只依赖标准库**，加一个 12 行的 `engine/base/result.h`（`Result<T> = std::expected`）。
它不认识 Qt，不认识任何渲染 API，不认识 tamias 的 `RHIDevice`。

```
                 ┌──────────────────────── 宿主程序（tamias / 你的软件）────────────────────────┐
                 │  读设置、读策略文件、读启动标记、解析命令行、写日志、持久化「上次可用」       │
                 └───────────────┬───────────────────────────────────────────┬──────────────────┘
                                 │ 提供输入                                   │ 实现 IBackendDevice
                 ┌───────────────▼───────────────────────────────────────────▼──────────────────┐
                 │                          tamias::rhi_startup                                     │
                 │                                                                                 │
                 │  decide_startup()  决策链：安全模式 > 策略 > 命令行 > 偏好 > 上次可用 > 兜底   │
                 │  probe_rhi()       按候选顺序试：建设备 → 查块名单 → 空提交 → 离屏像素         │
                 │  run_rhi_startup() 上面两步一步到位                                            │
                 │  parse_blocklist() / parse_policy()  数据驱动（坏驱动 / IT 策略）              │
                 └─────────────────────────────────────────────────────────────────────────────────┘
```

## 公开接口

| 头文件 | 内容 |
|---|---|
| `rhi_startup/backend.h` | `GraphicsBackend` 枚举 + 名字互转 |
| `rhi_startup/os.h` | 平台枚举（块名单要按平台区分驱动版本号写法） |
| `rhi_startup/identity.h` | `RhiGpuIdentity`（厂商 / 设备号 / 驱动版本…）+ 指纹 |
| `rhi_startup/blocklist.h` | `RhiBlocklist` / `RhiPolicy` + 解析（JSON 扁平子集） |
| `rhi_startup/decision.h` | `RhiStartupInput` / `RhiStartupDecision` / `decide_rhi_startup()` |
| `rhi_startup/device.h` | `IBackendDevice` + `BackendDeviceFactory`（**宿主接入点**） |
| `rhi_startup/probe.h` | `RhiProbeOptions` / `RhiProbeReport` / `probe_rhi()` |
| `rhi_startup/startup.h` | `RhiStartupEnvironment` / `run_rhi_startup()`（决策 + 探测） |

## 接入步骤（给另一个程序用）

1. **提供一个设备工厂。** 实现 `IBackendDevice`（最少只要 `gpu_identity()`；能有
   `submit_noop()` / `render_pixel_probe()` 就能开更深的探测档），再包成
   `BackendDeviceFactory`。tamias 自己的适配器见
   `src/engine/render/rhi/rhi_selection_device.cpp`，可以照着抄。
2. **读宿主侧的输入**：命令行 / 策略文件 / 设置里的偏好 / 上次可用后端 / 启动标记。
   组件不碰文件系统——这些是宿主的活。
3. **调用 `run_rhi_startup(环境, 工厂)`**，拿到 `chosen()`（本次后端）和 `report`
   （每个候选为什么被选中 / 跳过 / 失败，直接进日志）。
4. **按 `decision.remember_result` 决定要不要记「上次可用」。** 安全模式 / 策略强制 /
   命令行指定的会话**不该记**——否则一次崩溃循环会把机器永久降级。

## 单独拿走

这个目录不依赖 tamias 的其它源码（除 `engine/base/result.h`）。把它拷进你的工程，
带上 `result.h`（或换成你自己的错误类型），用 `add_subdirectory` 或
`FetchContent` 接进来即可，目标名 `tamias::rhi_startup`。

架构与行为细节：`docs/RHI-STARTUP.md`。测试：`tests/rhi_startup_tests.cpp`（17 条，全部不需要 GPU）。
