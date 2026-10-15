# 浏览器 wasm + 几何服务的分层调试

> **这条链路一共有四个调试面、三个进程、两套符号格式。** 想清楚"这个数是谁算的"，
> 比配好任何一个调试器都重要——[GEOMETRY-SERVICE-PLAN.md](https://github.com/terry-chao/tamias/blob/main/GEOMETRY-SERVICE-PLAN.md)
> 把造型求值搬到服务端之后，同一份特征树会在**三个地方**各算一遍（浏览器 wasm、
> 服务进程里的 DLL、桌面 `tamias.exe`），三者长得几乎一样。
>
> 关联：[Web 查看器](WEB.md)、[服务端语言决策](DECISION-SERVER-LANGUAGE.md)、
> [建模内核](MODELING-KERNEL.md)、[渲染场景快照](RENDER-SCENE.md)、
> [WebGL 链路审计](WEBGL-PIPELINE-AUDIT.md)。

---

## 0. 先建立心智模型：四层、三进程、两套符号

| 层 | 语言 / 进程 | 调试器 | 符号 |
|---|---|---|---|
| UI + 查看器壳 | TS/React，Chrome 渲染进程 | Chrome DevTools / VS Code `pwa-chrome` | sourcemap（Vite） |
| 引擎 + WebGL RHI | C++ → wasm，**同一个** Chrome 进程 | Chrome + DWARF（Emscripten `-g`） | **DWARF**（预设有 `-g` 才出） |
| 几何服务壳 | C# / ASP.NET Core，`Tamias.Server.exe`（`:5199`） | `coreclr`（C# 扩展） | PDB |
| 造型内核 | C++（OCCT）→ `tamias_server.dll`，**与上一条同一个进程** | `cppvsdbg` 附上去 | PDB（`RelWithDebInfo`） |

两套符号互不相通，这是最常见的误解来源：

- **wasm 里没有 PDB**，浏览器也不会读 PDB。调 C++ 必须靠 DWARF，而 DWARF 只有在
  带 `-g` 的产物里才有。`wasm` 预设默认**不带**（它是 `CMAKE_BUILD_TYPE=Release`）。
- **服务进程里没有 DWARF**，它是普通 Windows 进程。调 OCCT 靠 PDB，`build\bin\<Config>\`
  里那份 `tamias_server.pdb` 就在 DLL 旁边。

另外两个已经落地、别浪费的便利事实：

- **F5 链路已经一条龙**：`WebGL + .NET Server` 配置会编 wasm → 起 `:5199` → 起 `:3000`
  → 附 JS 调试器，脚本最后**打印 `RHI backend: ...`**（[wasm-preview.ps1](../scripts/wasm-preview.ps1)）——
  专门用来防"以为在调 WebGL，其实在跑 WebGPU"。
- 预览服务强制 `Cache-Control: no-store`，浏览器缓存旧 wasm 这个经典坑已经不在了
  （[WEB.md §5](WEB.md#5)）。

---

## 1. 按症状选工具

| 症状 | 先看哪里 | 关键证据 |
|---|---|---|
| 页面白屏 / 视口空 | DevTools Console + 页面日志面板 | wasm 加载了没有、`hasDocument()`、`backendName()` |
| 画面不对但没报错 | **Spector.js** 抓一帧 GL 调用 | draw call 数、绑的 VBO/UBO、混合状态 |
| 帧率低 | Performance 面板 record 一段 | 是 embind 边界、wasm 里的 tessellate、还是 GPU |
| wasm 里崩 | Chrome → Sources → wasm 帧（**要先编 `wasm-debug`**） | 没有 DWARF 只有 `wasm-function[1234]` |
| 服务端 500 / 起不来 | `TamiasGeometryServe` 窗口 | .NET 栈；原生异常已按纪律 catch |
| 原生 DLL 崩 / 结果错 | `cppvsdbg` 附到 `Tamias.Server` | `evaluate_feature_model`、OCCT 里的形状 |
| 两端结果不一致 | **三来源 digest 对比**（§4） | 特征树字节 / 网格字节 / 帧 |

```powershell
# 产物含 GL 胶水、不含 wgpu：确认"编进去的到底是哪个后端"最硬的证据
$js = 'build\wasm\bin\tamias_viewer.js'
$t = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($js))
'GLctx','glDrawElements','emscripten_webgl_create_context','wgpu' | % {
  '{0,-34} {1}' -f $_, ([regex]::Matches($t, [regex]::Escape($_))).Count
}

# 后端 / 内核开关
Select-String build\wasm\CMakeCache.txt -Pattern 'TAMIAS_ENABLE_(WEBGL|WEBGPU|OCCT|TRUCK)'
```

---

## 2. VS Code 里的四个配置

| 配置 | 什么时候用 | 能断在哪 |
|---|---|---|
| **WebGL + .NET Server**（F5） | 日常调 UI / 交互 / WebGL；它顺手把几何服务也起了 | TS/JS；wasm 帧只有在 `wasm-debug` 产物下才有源码 |
| **Geometry Server (attach)** | 要断 .NET（路由 / 会话 / P/Invoke 包装） | `Program.cs` / `NativeSession.cs`；服务没起时它的 `preLaunchTask` 会先起好 |
| **Geometry Server (native attach)** | 断点要落在 `tamias_server.dll`（`server_api.cpp`、命令派发、求值、OCCT） | 原生侧；**C# 断点在这个会话里不生效** |
| **WebGL 查看器 (wasm-debug, C++ 断点)** | 要断 wasm 里的 C++（§3） | `.cpp`；前提是页面加载的是 `wasm-debug` 产物 |

三条纪律：

1. **顺序永远是"先编 DLL、再编壳、最后开浏览器"**。服务壳和原生库之间有一次启动握手
   （[Program.cs:27-35](../server/csharp/Tamias.Server/Program.cs#L27-L35)），ABI 对不上
   **直接拒绝启动**——顺序错了你会在浏览器里浪费半天。`Tamias: 构建几何服务` 这条任务
   自己会**先停掉正在跑的 `Tamias.Server`**（否则 DLL 被占用，重链直接失败）再去编。
2. `Tamias.Server.exe` 和 `tamias_server.dll` **必须在同一目录**：P/Invoke 按约定的名字
   在 app base directory 里找 `tamias_server`，分开放就是启动即 `DllNotFoundException`
   （[wasm-preview.ps1](../scripts/wasm-preview.ps1) 的 `Resolve-GeometryDir` 强制这条）。
3. **5199 只有一个**：F5 那条链路、`attach` 的 `preLaunchTask`（[ensure-geometry-server.ps1](../scripts/ensure-geometry-server.ps1)）
   和手动起的 `serve-geometry.cmd` 都认这个端口。前两者是幂等的——已经在听就直接复用，
   所以 F5 之后马上 attach 是安全的；手动窗口和它们混用才会打架。

> 想在同一个会话里**从 C# 单步跨进 C++**（P/Invoke 边界）目前没有配好：托管调试器与
> `cppvsdbg` 各自管半边。跨界的排查办法是**在两侧各打一条日志 + 用 §4 的 digest 对齐**，
> 而不是指望一次单步穿过去。

---

## 3. 在浏览器里断进 C++（`wasm-debug` 预设）

`wasm` 预设是 `Release`，产物里没有 DWARF，Chrome 的 Sources 里只能看到
`wasm-function[1234]` 这种地址。调试专用的预设是：

```powershell
# 配置（只第一次；之后 cmake --build build\wasm-debug --target tamias_viewer 即可）
cmake --preset wasm-debug -G Ninja "-DCMAKE_TOOLCHAIN_FILE=C:/dev/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
cmake --build build\wasm-debug --target tamias_viewer
```

它等于 **`wasm-truck` + `CMAKE_BUILD_TYPE=Debug`**，并给 `tamias_viewer` 加三样
（[src/wasm/CMakeLists.txt](../src/wasm/CMakeLists.txt)）：

| 选项 | 作用 |
|---|---|
| `-g3` | 出 DWARF（含宏） |
| `-gsource-map` | 生成 `.wasm.map`，把 wasm 帧映射回 `.cpp` |
| `-sASSERTIONS=2` | 打开 Emscripten 运行时断言，浏览器里的 abort 信息直接可读 |

三个刻意的取舍：

- **它只编 Truck，不链 OCCT**。因为 `C:\dev\occt-wasm\prefix` 里**只有 Release 的 OCCT**，
  Debug 构建会安静地链到 `-O3 -DNDEBUG` 的 Release 库——那既不是能断点调试的 Debug，
  又让人以为"我编了 Debug"。`CMakeLists.txt` 里加了一条守卫，硬要把
  `EMSCRIPTEN + Debug + TAMIAS_ENABLE_OCCT` 凑在一起会**配置期就 FATAL**，而不是
  在链接期给你一个看不懂的错误。真要连 OCCT 一起调，得先交叉编译一份 Debug 的 OCCT。
- **它是独立的构建目录**（`build/wasm-debug`），不碰默认产物 `build/wasm/bin`。
  预览时把静态目录指过去即可：`scripts\serve-wasm.cmd <build\wasm-debug\bin> 3000`。
- **别用它量性能**。`-O0` + 断言 + ASYNCIFY 下的帧率没有参考价值；量性能回 `wasm` 预设。

配好后：Chrome DevTools → Sources → 找到 `.cpp` 直接断点。VS Code 里用现成的配置
**「WebGL 查看器 (wasm-debug, C++ 断点)」**（[.vscode/launch.json](../.vscode/launch.json)，
`webRoot` 已指向 `build/wasm-debug/bin`），但还要装 `ms-vscode.wasm-dwarf-debugging`——
vscode-js-debug 靠它读 DWARF。**注意**：DWARF 需要调试器在目标停下时读内存，
**在浏览器引擎内部断点（如 `glDrawElements` 里）会失效**——那种位置用 Spector.js 而不是断点。

### 症状对照：为什么断点只停在 JS 上

| 现象 | 原因 | 怎么办 |
|---|---|---|
| 暂停时只有 JS 栈，没有 `.cpp` 帧 | **当前跑的产物里没有 DWARF** | 确认它是不是 `build/wasm-debug/bin`。默认的 `build/wasm/bin` 是 `CMAKE_BUILD_TYPE=Release`、没加 `-g`，`.wasm` 里 `.debug_info` 命中数为 **0**（可自查，见下） |
| 明明编了 `wasm-debug`，还是停在 JS | **页面加载的是另一份产物** | F5 那条链路伺服的是 `build/wasm/bin`。要手动起：`scripts\serve-wasm.cmd <build\wasm-debug\bin> 3000` |
| `.cpp` 出现在 Sources 里，断点却是空心 | 缺 `ms-vscode.wasm-dwarf-debugging`，或 `webRoot` 指向了 `web/` | 用上面那条 VS Code 配置；Chrome 里则是没开 DWARF 支持（老版本 DevTools 需要 `chrome://flags` 里的 WebAssembly Debugging 开关） |
| 断点下在 `glDrawElements` / 驱动层 | DWARF 读内存的固有限制 | 换 Spector.js 抓帧 |

**自查产物有没有 DWARF**（0 就是没有）：

```powershell
$t = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes('build\wasm\bin\tamias_viewer.wasm'))
'.debug_info','.debug_line','.debug_str' | % { '{0,-14} {1}' -f $_, ([regex]::Matches($t,[regex]::Escape($_))).Count }
```

> **wasm 里的 C++ 和原生侧的 C++ 是两件事**。如果你要断的是 `tamias_server.dll`（OCCT、
> 命令派发、求值），那是 `Geometry Server (native attach)` 的活，用的是 PDB，**不需要**
> 上面这套 DWARF；反过来，浏览器里的 C++ 断点也看不到服务端那份代码。

---

## 4. 最有效的单一手段：三来源 digest 对比

这条链路最贵的 bug 是"同一份特征树，桌面 OCCT / 服务端 OCCT / 浏览器显示对不上"。
别用眼睛看，用哈希把范围切成三段：

```
特征树字节 digest ──► 服务端回包：每 mesh 的 (顶点数, 三角数, bounds, digest)
                              │
                    客户端 intern_mesh 后重算 digest
                              │
                    同一 .trscn 在桌面端算 digest
```

结论是唯一的：

| 对比结果 | 结论 |
|---|---|
| 特征树 digest 不同 | 客户端序列化 / 版本问题，与服务端无关 |
| 特征树同、服务端回包 digest 与桌面不同 | **求值层**（内核 / 弦高 / LOD 档位） |
| 两个几何 digest 都同、画面不同 | **渲染层**（RHI、sRGB、Z 映射、Y 翻转），这时才轮到 Spector.js |

工具：**`scripts/geometry-digest.ps1`**。

```powershell
# 端到端：把手写的 / 从客户端 dump 出来的特征树字节包成 TGEV 信封，POST 出去并解析回包
powershell -File scripts\geometry-digest.ps1 -Request tree.bin -SaveReply reply.bin

# 离线：解析一份已经抓到的回包（回归、CI、两次运行对比都靠它）
powershell -File scripts\geometry-digest.ps1 -Summarize reply.bin

# 机器可读，给金样测试用
powershell -File scripts\geometry-digest.ps1 -Summarize reply.bin -Json
```

它打印信封版本 / 状态 / 每条 `tag` 与每个 mesh 的 **LOD、顶点数、三角数、字节数、
bounds、SHA256**——正好是"服务端算出来的三角网"这一段的指纹。用 `-SaveReply` 落盘的那份
可以离线复算，于是"同一个输入两次跑结果不一样"这种问题不需要服务端在线也能复现。

**它现在会失败，而且是应该失败**：`POST /v1/geometry/evaluate` 还不存在
（[GEOMETRY-SERVICE-PLAN.md](https://github.com/terry-chao/tamias/blob/main/GEOMETRY-SERVICE-PLAN.md) 的 P0 仍未落地，今天服务只暴露
`/v1/sessions` 那套会话 ABI）。脚本遇到 404 会明说这一点，别误当成脚本坏了。

链路的另外两段：

- **特征树字节**：`write_feature_model` 的输出，客户端 dump 一份即可；它**对参数键排序**，
  字节稳定，可以直接当缓存键 / 对比基准。
- **帧 digest**：桌面端已有的 `.trscn` 机制（[渲染场景快照](RENDER-SCENE.md)）——状态栏
  与 `scene.meta.json` 里的 `digest`。跨后端一致性还可以直接用
  [`tests/kernel_conformance_tests.cpp`](../tests/kernel_conformance_tests.cpp) 的口径。

> 请求信封里的 `tag` 字段就是为这个准备的：**务必原样回显并打进两端日志**。多并发下
> 没有它没法把请求和结果对上。

---

## 5. 服务端这一侧：C# 与 OCCT 之间没有安全网

两条必须记住的纪律：

- **原生入口 `try/catch` 全包**（[DECISION-SERVER-LANGUAGE.md §4.1](DECISION-SERVER-LANGUAGE.md)）。
  异常穿过 `extern "C"` 进 .NET 是**进程崩溃**，不是异常——调试器只会给你一个
  "进程没了"，栈里看不到业务代码。
- **`default_kernel()` 是 `call_once` 单例，不是线程安全**（[GEOMETRY-SERVICE-PLAN.md §6.2](https://github.com/terry-chao/tamias/blob/main/GEOMETRY-SERVICE-PLAN.md)）。
  几何端点要么串行化要么加锁。这类竞态**在调试器下常因为时序改变而消失**——
  不要用"附上去就不复现"当成没问题。

OCCT 本身基本是黑盒，别指望单步进内核。有效的办法是在边界上**加读数**：

- 请求侧：特征树条数、`tag`、`deflection`、字节数；
- 求值侧：内核名与能力位（`KernelCapabilities`）、耗时、输出三角数 / 顶点数 / bounds；
- 响应侧：每个 mesh 的 digest（§4 的脚本会替你算）。

把这些打进服务窗口的日志，比在 OCCT 深处断点划算得多。

---

## 6. 八个"静默失败"，按踩到的顺序排

1. **旧产物**：`build/wasm/bin` 里的旧 wasm、`web/dist` 里的旧 bundle、旧
   `tamias_server.dll`。[src/wasm/CMakeLists.txt](../src/wasm/CMakeLists.txt) 的注释里
   已经记了两次真实翻车（"改了 C++ 却一直在跑旧 wasm""加了按钮却一直是旧 bundle"）。
   `no-store` 只防浏览器缓存，**不防构建产物本身是旧的**。
2. **ABI 错位**：DLL 与壳不同编 → 拒绝启动，或首次请求才炸。先看服务窗口。
3. **后端张冠李戴**：读脚本那行 `RHI backend:`，或页面上的 backend chip（`.tb-chip.exp`）。
   不要凭产物大小猜。
4. **CORS**：`:3000` ↔ `127.0.0.1:5199` 跨源，`server/` 里 `AddCors` 零命中
   （[GEOMETRY-SERVICE-PLAN.md §6.3](https://github.com/terry-chao/tamias/blob/main/GEOMETRY-SERVICE-PLAN.md)）。同源方案能顺带消掉这个噪声。
5. **Asyncify 在帧内挂起**：页面卡死、栈停在 `requestAnimationFrame` 里。纪律是死的：
   **网络只从命令路径发起，帧内只 poll**（[GEOMETRY-SERVICE-PLAN.md §7.4](https://github.com/terry-chao/tamias/blob/main/GEOMETRY-SERVICE-PLAN.md)）。
6. **静默不建管线**：WebGL 下 text / overlay / line 三条管线是**静默不建**的，日志里
   **不会有任何提示**（[WEBGL-PIPELINE-AUDIT.md](WEBGL-PIPELINE-AUDIT.md) 专门纠正了
   旧文档让你去日志里找的做法）。只能靠 Spector.js 数 draw call。
7. **`mesh_binary` 格式缺口**：`line_list` / `has_texcoord` / `faces` 缺失。跨边界传网格会
   **静默丢字段**——这也是 digest 要连"字段位掩码"一起覆盖的原因。
8. **调试器改变时序**：`default_kernel()` 竞态、Asyncify、rAF 三者都吃这个。
   附调试器不复现 ≠ 修好了。

---

## 7. 还没做的（想清楚再动手）

- **`/v1/debug/stats`**：进程内生调用计数、当前内核名与能力位、平均求值耗时。把
  "串行化有没有生效""内核是不是 OCCT"变成可读数字，比断点省事。
- **页面常驻 overlay**：backend 名 + 帧耗时 + pending 几何数 + **已加载产物哈希**。
  产物哈希那项直接冲着 §6.1 去。帧耗时读数在
  [WEBGL-EXP-IMPLEMENTATION-PLAN.md §4.1](https://github.com/terry-chao/tamias/blob/main/WEBGL-EXP-IMPLEMENTATION-PLAN.md) 里一直没做。
- **跨托管/原生单步**：需要调试器层面的混合模式，目前只能靠两侧日志 + digest 对齐。
