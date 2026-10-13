# RHI 卡死 / 崩溃定位：一次 NVIDIA 驱动现场复盘

> **状态**：**已定位并修掉**——崩在 `nvoglv64` 里的直接原因是 GL 变体的 SPIR-V 不合
> OpenGL 的规矩（分开的 image/sampler、SPIR-V 1.5），驱动接受后又在第一次
> `glUseProgram` 时崩在自己的线程里；改着色器编译方式即可（见 §10）。加上这次顺手修掉的
> 两个 OpenGL 后端问题与一处配色空间问题，OpenGL 后端在本机可以正常出图。
> 本文分两半：**方法**（怎么定位 RHI 层的卡死与崩溃，可以照着抄）与**现场**
> （2026-10-12/13 那次 RTX 5070 + NVIDIA 610.88 的完整证据链）。
>
> 适用范围：桌面 RHI（Vulkan / OpenGL）在「界面上不动了」「开窗就闪退」这类故障。
> 启动降级与安全模式的规则见 [RHI 启动](RHI-STARTUP.md)，OpenGL 后端本身见 [OpenGL 后端](OPENGL.md)。
> **所有结论标注了它是「代码事实」还是「推断」**——这次最大的教训就是别把推断当事实。

---

## 0. 一句话

同一份故障有两种表现：**先是卡住（驱动不返回），后是闪退（驱动自己线程里访问违例）**。
两次的故障线程都**不是** tamias 的线程，而是**驱动自建的线程**；我们的线程当时正卡在驱动内部等它。
最小 GL 探针（建设备 + 清一个像素 + 读回）能过，一画帧就死；崩点固定在 `nvoglv64+0x75ac6b`，
指令是拿一个**巨离谱的下标**去查表（`rax=0x29732528`）→ 读不可映射地址。
继续二分到最后，触发链是：**我们喂给驱动的 GL SPIR-V 不合规**（分开的 image/sampler →
`DescriptorSet 1..5`；模块还是 SPIR-V 1.5）→ 驱动 `glShaderBinary`/`glLinkProgram` 不报错
→ 第一次 `glUseProgram` 时才真正处理它 → 崩在驱动自己的线程里（§10）。
本机驱动（`nvoglv64.dll` 32.0.16.1088，自报 `PrivateBuild: dev build`）另有
`LiveKernelEvent 141`（视频引擎超时）记录。

---

## 1. 时间线：同一根因的两种表现

### 1.1 卡住（第一次报告）

| 时刻 | 事实 | 来源 |
|---|---|---|
| 21:19:41 | `RelWithDebInfo\tamias.exe` 启动，**同一秒 `vsdbg` 也启动**（调试会话拉起） | 进程表 |
| 21:19:55 | 打开文档走完：缩略图落盘、页签已加 | `%APPDATA%\tamias\tamias\thumbnails\` |
| 21:21 → 21:28 | CPU **零增量**；58 个线程里 **57 个 `Wait, Suspended`** | `Get-Process` |
| 同刻 | 渲染线程落点：`OpenGLBuffer::write → make_current_dummy → wglMakeCurrent → nvoglv64 → WaitForSingleObjectEx` | cdb 非侵入抓栈 |

**读法**：主线程停在**正常的 Qt 事件循环**里（`QCoreApplication::exec → QEventDispatcherWin32::processEvents`），
不在业务代码里等锁；全进程 `Wait, Suspended` 是「被调试器挂起」的特征（见 §5.1）。
所以那一次窗口为什么一动不动：**调试器把它停住了**；而渲染线程的最后落点在驱动里，
说明它当时已经陷在 `wglMakeCurrent` 的驱动锁上。

### 1.2 闪退（第二次报告）

| 时刻 | 事实 | 来源 |
|---|---|---|
| 21:38:02 | 直接双击启动（无调试器），运行 8 秒 | 转储头 `Process Uptime` |
| 21:38:07 | `.NET Runtime 1026`：进程因未处理异常终止，`c0000005 @ 0x7FFAC0D6AC6B` | 事件日志 |
| 21:38:08 | `Application Error 1000`：**故障模块 `nvoglv64.dll`**，异常 `0xc0000005`，偏移 `0x75ac6b` | 事件日志 |
| 21:38:09 | **`LiveKernelEvent` P1=141**（视频引擎超时）同秒记录 | 事件日志 |
| 21:38:10 | 本地转储 `%LOCALAPPDATA%\CrashDumps\tamias.exe.7860.dmp`（41 MB） | 磁盘 |

### 1.3 复现（这一步之后才有得查）

| 命令 | 结果 |
|---|---|
| `tamias.exe --probe-rhi --gpu-backend=opengl --json` | **通过**：`ok=true`，adapter `NVIDIA GeForce RTX 5070`，driver `32.0.16.1088` |
| `tamias.exe --render-view=<png> --gpu-backend=opengl <cube.obj>` | **每次 `0xC0000005`**，写不出 PNG（480×270 与 32×32 都一样） |
| 同上，`--gpu-backend=vulkan` | **通过**，正常写出 PNG |

---

## 2. 证据链：谁证明了什么

| 证据 | 证明了 | 排除了 |
|---|---|---|
| 事件 1000 的「故障模块 = `nvoglv64.dll`」+ 1026 的同地址 | 崩点在驱动镜像里 | 「崩在我们自己的某个函数」 |
| cdb 解转储：故障线程栈底是 `nvoglv64!… → BaseThreadInitThunk → RtlUserThreadStart` | 故障线程是**驱动自建**的线程 | 「我们调进去的 GL 调用当场崩了」 |
| 同一时刻我们渲染线程的栈：`draw_channel → nvoglv64 → GetExitCodeThread` | 我们的线程**正在驱动内部等它** | 「两边各干各的、只是巧合同时出错」 |
| `--probe-rhi`（建设备 + 清像素 + 读回）能过 | 驱动**不是**整体不可用 | 「显卡驱动装坏了/没有设备」 |
| 插桩：shader/管线创建全成功、两次 `draw_indexed` 正常返回、崩在 `end_frame` 的 `glFinish()` | 触发点在**指令流执行/同步**侧，不在 API 参数校验侧 | 「某个 GL 调用参数非法被驱动拒绝」 |
| 改动前后都崩、32×32 与 480×270 都崩 | **与「我们是否真发出 draw」无关、与尺寸无关** | 「某一次 draw / 某个尺寸触发」 |
| 系统事件：三天 13 次 `LiveKernelEvent 141`（多个无关程序）+ 驱动自报 `dev build` | 本机 GPU 栈**普遍**不稳 | 「只有 tamias 会触发」 |
| 交互式那次与离屏复现的崩点是**同一个地址** `+0x75ac6b` | 同一驱动缺陷的同一路径 | 「两种不同故障碰巧一起出现」 |

---

## 3. 定位手法清单（可复用）

### 3.1 先分清「卡住」还是「崩了」

- **崩了**：有事件日志（`Application Error 1000`）与转储 → 直接进 §3.2。
- **卡住**：进程还在、CPU 不涨。先看**线程等待原因**（§3.4），再说别的。
- 两者可以同源：驱动不返回是挂，驱动线程访问违例是崩（本文这次两者都出现了）。

### 3.2 Windows 侧的三类事件 + 转储

| 事件 | 关键字段 | 看什么 |
|---|---|---|
| `Application Error` **(1000)** | 出错应用程序 / **出错模块** / 异常代码 / 错误偏移 | 故障模块是 `.exe` 还是某个 `.dll`——**先看这一眼** |
| `.NET Runtime` **(1026)** | 异常代码 + 异常地址 | 和 1000 的地址是否一致（一致 → 同一现场） |
| `Windows Error Reporting` **(1001)** | `APPCRASH` / `LiveKernelEvent` + P1 | `LiveKernelEvent P1=141` 是**显示驱动超时**；`117` 是 TDR |

```powershell
# 最近 20 分钟的应用事件（含 1000 / 1001 / 1026）
Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddMinutes(-20)} |
  Select-Object -First 12 TimeCreated,Id,ProviderName,Message

# 本地崩溃转储（WER 自动落盘，不用调试器挂着也能拿到）
Get-ChildItem "$env:LOCALAPPDATA\CrashDumps" | Sort-Object LastWriteTime -Descending | Select-Object -First 5
```

### 3.3 用 cdb 解转储（不用挂进程，最安全）

```powershell
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $cdb -z "$env:LOCALAPPDATA\CrashDumps\tamias.exe.7860.dmp" `
       -y "C:\dev\tamias\build\bin\RelWithDebInfo" `
       -c ".ecxr; kv 40; lmvm nvoglv64; q"
```

读法：

1. `.ecxr` 切到**异常上下文**，`kv` 打它的调用栈；
2. **看栈底**：`... → BaseThreadInitThunk → RtlUserThreadStart` 之前最后一段是谁的代码。
   如果整段都是某个 `.dll`（这次是 `nvoglv64`），说明**这条线程是那个 dll 自己创建的**，
   故障发生在它的内部循环里，而不是被我们调进去的那一刻；
3. `lmvm <模块>` 看模块版本、路径、时间戳——这次顺带看到 `PrivateBuild: dev build`；
4. 同一个崩点在不同次事故里**偏移一致**（`+0x75ac6b`）→ 同一个可复现缺陷，不是随机损坏。

### 3.4 活体进程：非侵入抓栈 + 等待原因

进程还活着时，**先用非侵入方式**（`-pv`）抓栈：它不改目标状态、也不要求独占调试端口。
如果报 `The process that you are attempting to attach to is already being debugged`，
说明已经有调试器（这次是 `vsdbg`）挂着它——**这时先怀疑调试器把人停住了**，见 §5.1。

```powershell
& $cdb -pv -p <pid> -y "<bin 目录>" -c "~*kv; q"
```

不装调试器也能拿到一份很有用的信息：**每个线程的等待原因**

```powershell
Get-Process -Id <pid> | ForEach-Object { $_.Threads } |
  Group-Object ThreadState,WaitReason | Sort-Object Count -Descending
```

### 3.5 无窗口复现：三个现成开关

定位 RHI 问题最怕「得开窗口才能测」。tamias 自带三条无窗口通道：

| 开关 | 跑到哪一步 | 用途 |
|---|---|---|
| `--probe-rhi --gpu-backend=<后端> --json` | A 档建设备 / B 档空提交 / **C 档离屏画一个像素再读回** | 最快判断「这个后端能不能出图」，且**不写启动标记** |
| `--render-view=<png> --render-size=<WxH> <文档>` | 完整渲染一帧并读回存 PNG | 复现「画帧才死」这类问题；也用来确认修好没有 |
| `--gpu-backend=<vulkan\|opengl>` | 强制后端 | 把变量压到「只剩后端」一个 |

注意两点：

1. **必须用 `=`**：`--render-view=<png>`。写成 `--render-view <png>` 会被当成裸开关，
   于是 `png` 变成位置参数 → 走 GUI 路径去打开这个「不存在的文档」 → 弹模态警告框等你点。
2. 无窗口站/无人值守的会话里，**GUI 路径会卡在模态框上**（它不是死锁，是没人点「确定」）。

### 3.6 二分与插桩

从「一帧全流程」往下砍，每次只砍一刀：

```
尺寸（32×32）→ 场景（空网格 / 单三角）→ 是否真的发 draw → 具体 GL 调用
```

这次砍到「是否真的发 draw」就切开了：改动前后（一个发、一个不发）**都崩**，于是
「我们某条 draw 命令非法」被排除，触发点落到驱动对指令流/帧同步的处理上。
插桩用现成的 `log_info`（日志默认 Info 级），打完记得删干净。

### 3.7 交叉证据

- **系统级 GPU 事件**：`LiveKernelEvent 141` / `nvlddmkm` 报错 → 整机 GPU 栈的问题，不是单个程序；
- **驱动元数据**：`lmvm` 或文件属性里的 `PrivateBuild`、时间戳、版本号；
- **块名单**：`assets/rhi_blocklist.json` 是「已知坏驱动」的落点，确认后应该写进去（见 [RHI 启动](RHI-STARTUP.md) §3）。

---

## 4. 这次得出的结论

| 结论 | 证据强度 |
|---|---|
| 崩溃现场在 **NVIDIA OpenGL 驱动（`nvoglv64.dll`）自己的线程内部** | 强：故障线程栈里没有一帧 tamias 代码；同一崩点（`+0x75ac6b`）多次复现 |
| 触发它的输入在 **我们（tamias）这边**：GL 变体的 SPIR-V 不合 ARB_gl_spirv | 强：§10 的二分——只保留链接好的程序 + 第一次 `glUseProgram` 就崩；改成合并 image/sampler 后不崩 |
| 本机驱动版本 **32.0.16.1088（610.88）** 且自报 `dev build`，三天 13 次显示驱动超时 | 强（事实）|
| 「OpenGL 路径在这台机器上不可用」是**修之前**的结论 | 已作废：§10 修完后探针、离屏出图、GUI 三条路都正常 |
| 驱动内部**具体**是哪个逻辑错了（为什么会从这个下标查表） | **未知**（转储只有驱动镜像；`!` 后面的函数名不可信，见 §5.2）。**驱动该做的是**：`glShaderBinary` / `glLinkProgram` 当场报 `GL_INVALID_VALUE`/链接失败，而不是接受后崩 |

---

## 5. 三个陷阱（别被它们带偏）

### 5.1 `Wait, Suspended` 不是死锁

一开始看到「58 个线程里 57 个 `Wait, Suspended`」，很容易判成死锁。
拿同机的健康进程做对照才发现：正常进程的线程等待原因是
`UserRequest` / `Executive` / `EventPairLow` / `Unknown`，**从不出现 `Suspended`**；
`Suspended` 是「整进程被挂起」的特征（调试器断住、或 `SuspendThread`）。
那次真正的解释是：**调试器把进程停住了**（`vsdbg` 与 `app` 同秒启动）。

> 判据：**先找对照进程**。同一台机器上同类进程的等待原因分布，就是你的基线。

### 5.2 `!` 后面的函数名可能离谱，先看偏移

转储里出现 `nvoglv64!DrvPresentBuffers+0x19568e`、`DrvValidateVersion+0xe018` 这类符号。
驱动镜像的导出符号极少，dbghelp 只能拿**最近的导出符号**凑名字，偏移量（几十万字节）说明
这些名字和真实函数**没关系**。别据此写「驱动在 present 路径崩了」这种结论——
只能说「在 `nvoglv64` 内部、偏移 `0x75ac6b`」。

### 5.3 沙箱/隔离会话里的「没有窗口」不成立

在受限会话里 `EnumWindows` 可能返回 0 个窗口（窗口站是隔离的），
`Get-Process` 的 `MainWindowHandle` 也是 0。这不是「进程没有窗口」的证据，别写进结论。
同理 `Get-CimInstance Win32_Process` 查命令行也可能被拒绝。

---

## 6. 顺手修掉的两个 OpenGL 后端问题

排查过程中查了 GL 后端的上下文管理，发现两处**代码事实**级的问题（都已修）：

### 6.1 帧内把上下文 release 掉，后面的 draw 全被丢

原来每个 GL 操作都是「`make_current_dummy()` → 干事 → `release_current()`」：

```cpp
Result<void> OpenGLBuffer::write(...) {          // 修前
  if (auto r = device_->make_current_dummy(); !r) return Err(r.error());
  gl::BindBuffer(target_, buffer_);
  gl::BufferSubData(...);
  device_->release_current();                    // ← 帧内也这么干
  return {};
}
```

而 `OpenGLCommandList` 的 `set_*` / `draw_indexed` **不做** make_current（它们假定帧内上下文一直是 current 的）。
于是「帧内写一次实例缓冲」就把窗口上下文踢掉了，**其后的 `glDrawElements` 全部在
没有 current context 的情况下发出**——GL 调用此时不产生任何效果。

换句话说：**OpenGL 后端此前基本只画得出清屏色**，模型部分是被静默丢掉的。

### 6.2 dummy 窗口用的是系统共享 DC

dummy 窗口原来用 `STATIC` 类建（`CreateWindowExA(0, "STATIC", ...)`），
它拿到的 DC 来自系统的公用 DC 缓存，却被当成 GL drawable 用；GL 子窗口那边
（`tamias_gl_surface_class`）本来就是 `CS_OWNDC` 的自有类，只有 dummy 漏了。已改成自有 `CS_OWNDC` 类。

### 6.3 上下文切换风暴

修前：**每次** GL 操作都配一对切换——`make_current_dummy()` + `release_current()`。
帧内每个批次写一次实例数据就是 2 次驱动调用，批次数上百的场景一帧上千次；
而 `wglMakeCurrent` 每次都要进**驱动全局锁**（这次两次事故的栈都停在这个函数里）。
修后：设备记住「当前挂在哪个 drawable」，只在目标真的变化时才切——**帧边界（`begin_frame` / `end_frame`）
各一次**，资源操作不再来回切。

> 三处改动都在 [opengl_device.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/opengl/opengl_device.cpp)。
> **它们没有修好这次崩溃**（崩在驱动里），但 6.1 是实打实的功能缺陷，6.3 是实打实的性能与稳定性问题。

---

## 7. 两个策略坑：安全模式会把机器推向更坏的那条路

> **2026-10-13 复查：两处都已修掉**（[决策顺序](RHI-STARTUP.md) §1 / §5、标记内容、CLI 不写标记）。
> 下面保留当时的现场描述与推理——它是这两处改动的理由，也是「为什么命令行必须压过标记」的原始证据。

### 7.1 启动标记强制 OpenGL，而且压过命令行

[RHI 启动](RHI-STARTUP.md) §5 的启动标记本来是好事（坏驱动不该变成「每次开机都崩」），
但它假定**安全模式那条路是好的**。在这台机器上：

```
GL 崩一次 → 留下 startup.marker → 下次启动无条件走「安全模式 = OpenGL」
          → 又崩 → 标记还在 → ……（自锁循环）
```

而且决策顺序里**标记排在命令行之前**（[decision.cpp](https://github.com/terry-chao/tamias/blob/main/src/rhi_selection/decision.cpp)）：
实测传了 `--gpu-backend=vulkan`，日志里仍然是
`Creating RHI device: OpenGL` + `RHI startup: previous startup did not finish; forcing safe mode (OpenGL)`，
然后**当场复现了「卡住」**（渲染线程进驱动不返回）。
也就是说：**只要标记在，命令行都救不回来**，只能删标记 / 换驱动。

### 7.2 CLI 出图也会写这个标记

`main.cpp` 里 `startup_guard.begin()` 在 `--render-view` 分支**之前**调用，
所以 `--render-view` 跑完（或崩掉）都会留下标记；而旁边的注释写的是
「上面那些命令行模式不建窗口、跑完就退出，标记不该被它们碰」——**代码与注释不一致**。
后果：**任何一次出图，都会把下次 GUI 启动推进安全模式**。

---

## 8. 还没证实的

1. 驱动内部到底哪一步错了（要 NVIDIA 侧的符号或源码，我们没有）。
2. ~~我们的 GL 用法里有没有「合法但踩中该驱动缺陷」的组合~~ → 已证伪：SPIR-V／实例化属性／
   FBO 与窗口共用上下文都单独剥过（§10），触发点收敛到「**不合规**的 SPIR-V」。
3. 换一份稳定驱动之后 OpenGL 是否可用：**没测**（需要换驱动）；现在不需要换驱动也能用（§10）。
4. 这个驱动是不是对**任何**不合规 SPIR-V 都崩（我们只试出「分开的采样器」这一种不合规）：
   给 NVIDIA 报的时候用 §10 的最小复现就够了，不用等答案。

---

## 9. 待办

| 项 | 说明 | 归属 |
|---|---|---|
| ~~块名单加一条~~ ✅ 2026-10-13 → ❌ **2026-10-13 撤掉** | 当时按「OpenGL 一画帧就死」加了 NVIDIA 610.88 → `force: vulkan`。§10 找到真正原因（我们的 SPIR-V 不合规）并修好后，这条**已删除**（否则会把本来能用的 OpenGL 一直挡掉）。两个后端驱动字符串写法不同的经验保留在 [RHI 启动](RHI-STARTUP.md) §3 | `assets/rhi_blocklist.json` |
| ~~安全模式优先 `last_good_backend`~~ ✅ 2026-10-13 | 改成「按自然顺序 + 把标记里那个后端排到最后」：标记现在记着「当时用的是哪个后端」，不用再猜，也不再硬编码 OpenGL | [decision.cpp](https://github.com/terry-chao/tamias/blob/main/src/rhi_selection/decision.cpp)、[startup_guard.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/base/startup_guard.cpp) |
| ~~CLI 出图不写启动标记~~ ✅ 2026-10-13 | `startup_guard.begin()` 移到 `--render-view` 分支之后 | [main.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/main.cpp) |
| ~~命令行优先于标记~~ ✅ 2026-10-13 | 顺序改为：`--safe-mode` > 策略 > `--gpu-backend` > 标记 > 偏好 / 记忆 | [decision.cpp](https://github.com/terry-chao/tamias/blob/main/src/rhi_selection/decision.cpp) |
| **GL 端加像素级测试** | 现有离屏测试只断言**清屏色**，所以 §6.1「draw 全被丢」能一路绿灯——补一条「画一个已知三角，断言像素」 | [offscreen_render_tests.cpp](https://github.com/terry-chao/tamias/blob/main/tests/offscreen_render_tests.cpp) |
| 驱动侧复核 | 换 WHQL 稳定版后重跑 `--probe-rhi --gpu-backend=opengl` 与 `--render-view` | 人工 |

**改完怎么验（本机实测，2026-10-13）**：

```powershell
# 1) OpenGL 直选（不再被块名单改道）
& tamias.exe --probe-rhi --gpu-backend=opengl --json
#    "chosen": "OpenGL",  "summary": "RHI: using OpenGL"

# 1b) 真出一张图（反复跑都该 exit 0 并写出 PNG）
& tamias.exe "--render-view=$env:TEMP\gl.png" --gpu-backend=opengl --render-size=480x270 assets\samples\cube.obj
#    PNG 的亮度应当和 --gpu-backend=vulkan 那张一致（同场景 mean luminance 70.6，逐像素差 ≤1）

# 2) 纯决策层的回归（无需 GPU）
& tamias_tests.exe --gtest_filter="Rhi*"

# 3) 离屏渲染回归（含 OpenGL 清屏 + 读回）
& tamias_tests.exe --gtest_filter="OffscreenRender.*"
```

---

## 10. 真正的问题：GL 变体的 SPIR-V 不合 ARB_gl_spirv（2026-10-13 补）

§1–§9 的结论「故障在驱动侧」只对了一半：**崩在现场的是驱动，把它推到那一步的输入是我们的**。
把「喂什么给驱动」当作变量重新二分之后，触发点收敛到一行：**第一次对 SPIR-V 程序调
`glUseProgram`**。

### 10.1 怎么二分出来的

临时在 GL 后端与渲染线程里插了按环境变量开关（`TAMIAS_GL_ABLATE=`）的砍刀，每组只砍一处：

| 砍掉什么 | 结果 |
|---|---|
| 只留「绑 FBO → `glClear` → `glFinish`」两帧（不建 GL 对象、不发 draw） | **不崩**（写出 PNG） |
| 建对象但**不发 draw** | 崩（第 2 帧 `glFinish` 处） |
| 只跳过 `glCreateProgram` / `glAttachShader` / `glLinkProgram`（返回空管线） | **不崩** |
| 只跳过 `glLinkProgram`（程序没链过，`glUseProgram` 是空操作） | **不崩** |
| 正常链接，但跳过 `glUseProgram` | **不崩** |
| 正常链接 + 正常 `glUseProgram` | 崩（第 1 帧 draw #2 附近） |

结论：**必须是「链接成功的程序」+「第一次 `glUseProgram`」**，缺一不崩——这是驱动真正把
SPIR-V 落地成可执行 GL 程序的时刻。

### 10.2 独立复现（不含 tamias 的渲染代码）

`build/glmin/glmin.cpp`：约 200 行 Win32 + WGL 的最小程序——隐藏窗口 → 4.5 Core 上下文 →
FBO → 用 tamias 的 `mesh.vert.gl.spv` / `mesh.frag.gl.spv` 走
`glShaderBinary` + `glSpecializeShader` + link → `glUseProgram`。

- 不调 `glUseProgram`：正常（3 帧跑完、读回正常）。
- 换成随手写三行 HLSL 编出来的 SPIR-V：正常。
- 只把一侧（VS 或 FS）换成随手写的：接口对不上，链接就失败，不崩。
- **两个模块都用 tamias 的**：崩，且和 tamias 里落在同一个地址 `nvoglv64+0x75ac6b`，
  寄存器也是同型（拿一个巨大下标去查表）：

```
nvoglv64+0x75ac6b: mov rbx, qword ptr [rsi+rax*8+7EFC8h]     ; rax=0x29732528
```

「是不是 tamias 的绘制逻辑有问题」就此排除：驱动只看到两个 SPIR-V 模块和一次 `glUseProgram`。

### 10.3 根因：模块不合 GL 的规矩

三个片元着色器的 **GL 分支**（`#else`）原来写的是 `Texture2D ... : register(t0)` +
`SamplerState ... : register(s0)`，dxc 会翻译成**分开的 image 变量与 sampler 变量**，
并把 `t0..t5` 摆成 **`DescriptorSet 0..5`**：

```spirv
%albedo_tex  = OpVariable ... UniformConstant      ; OpTypeImage
%albedo_samp = OpVariable ... UniformConstant      ; OpTypeSampler
%161 = OpSampledImage %type_sampled_image %159 %160   ; 用的时候才临时合起来
OpDecorate %normal_tex DescriptorSet 1  Binding 0
```

而 `ARB_gl_spirv` 要求：采样的 image+sampler 必须是**一个合并变量**
（`OpTypeSampledImage` 类型的变量），`DescriptorSet` 只能是 0（GL 只认 binding = 纹理单元）。
另外 GL 只接受 **SPIR-V 1.0**，GL 变体当时编的是 `-fspv-target-env=universal1.5`（1.5），
`spirv-val --target-env opengl4.5` 直接判非法：

```
error: line 0: Invalid SPIR-V binary version 1.5 for target environment SPIR-V 1.0 (under OpenGL 4.5 semantics).
```

两处都不合规，而 NVIDIA 的 `glShaderBinary` / `glLinkProgram` 都没报错（这份驱动的缺陷：该拒的
输入没拒），真正的处理被推到第一次 `glUseProgram`，然后崩在它自己的线程里。

### 10.4 修法（三处，都在仓库里）

1. `shaders/{mesh,overlay,text}.frag.hlsl` 的 GL 分支改成**合并**的 image+sampler，binding 就是
   纹理单元，和 RHI 的 `kTextureSlot*` 对齐（0=albedo、1=normal、2=IBL 辐照、3=IBL 预滤、
   4=BRDF LUT、5=ORM）：

   ```hlsl
   [[vk::combinedImageSampler]] [[vk::binding(0, 0)]] Texture2D albedo_tex;
   [[vk::combinedImageSampler]] [[vk::binding(0, 0)]] SamplerState albedo_samp;
   ```

2. `cmake/TamiasShaders.cmake`：GL 变体的 `-fspv-target-env` 从 `universal1.5` 改成
   `vulkan1.0`——dxc 没有 OpenGL 目标，`vulkan1.0` 是唯一能产出 **SPIR-V 1.0** 的档；换完之后
   `spirv-val --target-env opengl4.5` 对 8 个 GL 模块全部通过。
3. 顺带修掉一个被崩溃掩盖的显示问题：离屏 FBO 的颜色附件 `GL_RGBA8` → `GL_SRGB8_ALPHA8`，
   并在绑 FBO 时 `glEnable(GL_FRAMEBUFFER_SRGB)`（sRGB 转换默认是关的）。此前 GL 出的图比
   Vulkan 明显偏暗（同场景平均亮度 25 vs 71），修完两张图**逐像素差 ≤1、平均亮度都是 70.6**。

### 10.5 修完怎么验（本机实测）

```powershell
& tamias.exe --probe-rhi --gpu-backend=opengl --json      # chosen: OpenGL（不再被改道）
& tamias.exe "--render-view=$env:TEMP\gl.png" --gpu-backend=opengl --render-size=480x270 assets\samples\cube.obj
                                                          # 连跑 3 次都 exit 0 并写出 PNG
& tamias_tests.exe --gtest_filter="OffscreenRender.*"      # OpenGL 清屏 + 读回，过
& tamias.exe assets\samples\cube.obj                      # GUI：建 GL 子窗口、上传纹理、连续出帧不崩
```

### 10.6 留给驱动侧的两件事

- 报给 NVIDIA 的最小复现就是 `build/glmin/`：一个 exe + 两个 `.gl.spv`，10 秒内必崩在
  `nvoglv64+0x75ac6b`（`0xC0000005`），附带那两条寄存器现场的记录。
- 我们这边**不再**为它留块名单条目：输入已经合规、OpenGL 可用；真要强制某后端，用策略文件
  （见 [RHI 启动](RHI-STARTUP.md) §4）比写死在块名单里合适。

### 10.7 这次的教训

1. **「驱动接受了」不等于「输入合法」**：SPIR-V 要过 `spirv-val --target-env opengl4.5`
   这类目标环境校验，别拿「链接成功」当合格证。
2. 两个后端共用一套 HLSL 时，**GL 变体不能照抄 Vulkan 的写法**：分开的 image/sampler、
   非 0 的 descriptor set、SPIR-V 1.5，三样都是 GL 不认的。
3. 崩在驱动里，先问「我喂了什么」：把输入当变量二分（这次的砍刀开关），比盯着驱动反汇编快得多。

---

## 附录 A：命令清单（可直接抄）

```powershell
$exe = "C:\dev\tamias\build\bin\RelWithDebInfo\tamias.exe"
$doc = "C:\dev\tamias\assets\samples\cube.obj"

# 1) 这个后端能不能出图？（无窗口、不写启动标记）
& $exe --probe-rhi --gpu-backend=opengl --json

# 2) 完整画一帧并出图（注意 = ）
& $exe "--render-view=$env:TEMP\gl.png" --gpu-backend=opengl --render-size=480x270 $doc
& $exe "--render-view=$env:TEMP\vk.png" --gpu-backend=vulkan --render-size=480x270 $doc

# 3) 崩溃现场
Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000,1001,1026; StartTime=(Get-Date).AddHours(-2)} |
  Select-Object TimeCreated,Id,ProviderName,Message
Get-ChildItem "$env:LOCALAPPDATA\CrashDumps" | Sort-Object LastWriteTime -Descending | Select-Object -First 3

# 4) 活体进程：非侵入抓栈 + 等待原因
& 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe' -pv -p <pid> -y "C:\dev\tamias\build\bin\RelWithDebInfo" -c "~*kv; q"
Get-Process -Id <pid> | ForEach-Object { $_.Threads } | Group-Object ThreadState,WaitReason

# 5) 启动标记（在就删掉，否则下次被强制安全模式）
$m = "$env:APPDATA\tamias\tamias\startup.marker"; Test-Path $m
```

## 附录 B：涉及文件

| 主题 | 文件 |
|---|---|
| OpenGL 后端（本次改动在这里） | [opengl_device.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/opengl/opengl_device.cpp) |
| 后端决策（安全模式 / 标记 / 命令行顺序） | [decision.cpp](https://github.com/terry-chao/tamias/blob/main/src/rhi_selection/decision.cpp) |
| 启动标记 | [startup_guard.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/base/startup_guard.cpp) |
| 命令行解析（`--probe-rhi` / `--render-view`） | [rhi_cli.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/base/rhi_cli.cpp) |
| 离屏帧与读回 | [render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.cpp) |

## 延伸阅读

- [RHI 启动](RHI-STARTUP.md)：探测档位、块名单、安全模式、决策顺序
- [OpenGL 后端](OPENGL.md)：这块后端在整条链里干什么、上下文怎么管
- [管线与 RHI](RENDERING.md)：一帧的绘制顺序与 RHI 抽象
- [测试](TESTING.md)：现有测试能断言什么、不能断言什么
