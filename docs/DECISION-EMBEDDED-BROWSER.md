# 决策：什么时候需要引入「Chrome 嵌入」的浏览器库

> 回应一个反复出现的需求：「什么时候要加 chrome 嵌入的那个库？」结论先行：**默认不加。**
> 只有同时命中下面第 3 节的硬门禁，才引入；引入什么形态（QtWebEngine / CEF / WebView2）由
> 「网页要不要和三维视口同帧合成」决定，而不是由「我们有个网页」决定。

---

## 0. 一句话结论

| 需求长相 | 该怎么做 |
|---|---|
| 「打开我们的文档 / 官网 / 云平台」 | `QDesktopServices::openUrl`，**不加库** |
| 「在 app 里看一个网页」 | 外部浏览器 + 本地桥（自定义协议 / localhost），**先不加库** |
| 「网页要和 app 里的文档双向联动」 | 命中门禁 → 独立面板形态最省事（QtWebEngine） |
| 「网页画面要和三维视口叠在一起 / 跟相机联动」 | 命中门禁 → CEF + OSR（离屏渲染合成进自研 RHI），按周计的工程量 |
| 「整个桌面 UI 改成 React 跑在 WebView 里」 | 这是**架构重写**，不是加个库，得单独立项 |

判断依据不是「有没有网页」，而是「这段网页**必须活在 Tamias 窗口里**，还要**分走文档的会话和交互**」。

---

## 1. 先把术语对齐：「chrome 嵌入库」是四个不同的东西

| | 是什么 | 许可 | 平台 | 典型的「为什么要选它」 |
|---|---|---|---|---|
| **QtWebEngine** | Qt 官方模块，内嵌 Chromium（含 `QWebEngineView`、`QWebEnginePage`、`QWebChannel`） | LGPLv3（部分组件 GPLv3）/ 商业 | Win / Linux / macOS | 桌面已经是 Qt 壳，网页只当一个面板 |
| **CEF**（Chromium Embedded Framework） | 社区维护的 Chromium 封装，C++ API，支持离屏渲染（OSR） | BSD-3（Chromium 本身也是 BSD 系） | Win / Linux / macOS，自己编 | 要把网页画面**合成进自研渲染器**，或要自己控制 Chromium 版本 |
| **WebView2** | 微软 Edge 的嵌入口，复用系统的 WebView2 Runtime | 专有（免费分发） | **仅 Windows** | 只发 Windows、想省体积、不想编 Chromium |
| **Electron** | 反过来：把整个 app 装进 Chromium + Node | MIT | 三平台 | 做纯 Web 桌面应用；对 Tamias **不适用** |

非 Chromium 系（Sciter、Ultralight 之类）体积小，但兼容性 / 生态 / 授权是另一笔账，不作为首选。

**许可先例在本仓库里已经有了**：[DRAWING.md](DRAWING.md) 第 5 节，`Qt6::Pdf` 因为 Qt 6 下是
GPLv3 / 商业双许可（LGPL 不覆盖），被做成 `find_package(... QUIET)` 可选项，没装就退回 DXF/SVG/图片。

注意区别：**QtWebEngine 走 LGPLv3 路线，闭源产品可以用**，代价是动态链接 + 允许用户替换该库
（和 Qt 本体同一套合规工作）。真要「零合规负担」，BSD-3 的 CEF 反而更干净，但构建和 CVE 跟进全归自己。

---

## 2. 需求侧：哪一类需求真的需要它

### A. 命中就基本要嵌（网页本身就是功能）

1. **第三方插件用 HTML/JS 写 UI**。对标 VS Code 的 Webview、Revit 新版 Web 面板。
   —— Tamias 当前插件 UI 只能走宿主 Qt 对话框（[插件设计](plugin/design.md) 明确「不给插件自建 HWND /
   嵌入 WinForms」）。插件作者要做复杂表格、图表、动态表单时，会直接顶到这个天花板。
2. **云协同 / 校审平台的网页端嵌进 app**（BIM 360、广联达、品茗那类），要求共享登录态，
   并且能双向跳转：点平台上的问题 → 三维里定位构件，反之亦然。
3. **在线族库 / 构件库**：浏览、预览、一键入库，登录与本机会话打通。
4. **在线 GIS 底图 / 地图服务**（高德、百度、天地图、Mapbox、3D Tiles）。只有 JS SDK，
   没有等价可控的 C++ SDK；要在 app 里关联工程位置，基本只能嵌浏览器。
5. **只发布为 Web 的 JS 生态工具**：DWG 在线标注（mxcad、Autodesk Viewer）、pdf.js 定制、
   web-ifc / IFC.js 的部分能力、ECharts 这类**需要交互**的报表看板。
6. **带登录 / 搜索 / 内嵌能力的帮助中心**：弱触发，通常外部浏览器就够，列在这里只是别漏判。

### B. 常被误判成「需要嵌」的（其实不需要）

| 需求 | 不用 Chromium 的做法 |
|---|---|
| 静态帮助 / 单页说明 | `QTextBrowser` 或 `QDesktopServices::openUrl` |
| 普通参数表单、属性面板 | Qt Widgets / QML（现有栈） |
| 离线报表、工程量表 | 生成 HTML → 无头渲染成 PDF，或直接用 `QTextDocument` 排版 |
| 看一眼自家的 Web 查看器 | **Tamias 已有 WASM 线**（[WEB.md](WEB.md)），外部浏览器打开更省事、更稳 |
| OAuth / SSO 登录 | 系统浏览器 + 本地回环回调；内嵌浏览器反而常被厂商策略禁止 |
| 只读的在线模型预览 | 服务端转成 glTF/`.trscn` 再进现有视口，比嵌浏览器轻得多 |

### C. 反例

「为了桌面和 Web 统一 UI 语言，把整个桌面界面改成 React，跑在 WebView 里」。这不是「加个库」，
是把 Qt 壳换掉。若真走这条路，本文第 4.6 节的决定要先做完。

---

## 3. 门禁：五问齐备才立项

| # | 问题 | 答「否」时的动作 |
|---|---|---|
| **Q1** | 内容是否**只能**以 HTML/JS 交付，没有 API、无头渲染或数据转换的替代路径？ | 走替代路径，不加库 |
| **Q2** | 是否**必须**驻留在 app 窗口内（开外部浏览器在体验上不可接受）？ | 外部浏览器 + 本地桥 |
| **Q3** | 是否需要与文档**双向交互**（网页 ↔ 选中构件 / 命令 / 参数）？ | 只读展示的话，嵌入的收益不足以覆盖体积与维护成本 |
| **Q4** | 是否需要**共享会话 / 本地存储 / 文件系统**（登录态、缓存、拖拽上传）？ | 同上，收益不足 |
| **Q5** | 这笔收益是否**值**百 MB 级安装体积 + 每月跟进 Chromium 安全更新 + 多维护一套 UI 技术栈？ | 一次性需求就别嵌，做成外部页面 |

**读法**：Q1 + Q2 是硬门槛，缺一不立项；Q3 / Q4 决定「嵌得值不值」；Q5 是否决权。
四条全中、Q5 也点头，再谈选型。

---

## 4. Tamias 特有的代价（重点，别跳过）

### 4.1 合成与窗口层：这是最大的坑

桌面视口是**原生子窗口**（Vulkan 一个 HWND，OpenGL 另建一个，见
[`document_viewport.cpp`](https://github.com/terry-chao/tamias/blob/main/src/app/document_viewport.cpp)），
而且仓库已经为「原生子窗口吃掉 Win32 鼠标消息、叠放顺序」写过专门的补丁
（[`box_select_overlay.cpp`](https://github.com/terry-chao/tamias/blob/main/src/app/box_select_overlay.cpp)、
[`view_cube_widget.cpp`](https://github.com/terry-chao/tamias/blob/main/src/app/view_cube_widget.cpp)）。

- **网页只做独立 dock / 独立标签页**（不叠在三维视口上）：风险可控，优先这个形态。
- **网页要叠在三维视口上**（HTML 浮层、地图底图跟相机联动）：`QWebEngineView` 基本不是答案
  ——它内部走 GPU 合成，和原生 HWND + 自研 Vulkan swapchain 同窗共存，z-order、DPI、输入穿透
  都得实测。合理路径是 **CEF + OSR**：网页离屏渲染到纹理，再由自研 RHI 采样合成，
  同时自己做输入转发。这是个按周计的子项目，不是 `find_package` 一行。

### 4.2 进程与部署

Chromium 是多进程的：要打包 `QtWebEngineProcess`、resources、locales、ICU 数据；
沙箱环境、杀软、企业内网策略都可能出问题。Windows 线还要确认
`tamias_deploy_qt_runtime` / windeployqt 把 WebEngine 运行时完整收集。

### 4.3 体积与构建成本

Release 安装包增量在**百 MB 量级**（`QtWebEngineCore` 单个 DLL 就接近百 MB，另加 resources/locales），
Debug 构建树可上 GB；运行时内存基线每个进程百 MB 级。构建侧：Windows 现用官方 Qt
（`CMakePresets.json` 的 `TAMIAS_QT_PREFIX=C:/Qt/6.11.1/msvc2022_64`，
**当前这台机器没装 WebEngine 组件**，要先用 Maintenance Tool 装），Linux 线走 vcpkg，
`qtwebengine` 是出了名的重 port（小时级构建，CI 时间直接上涨）。

### 4.4 安全维护：嵌了就得管

引入浏览器 = 你负责它承载的 Chromium CVE。要么跟着 Qt 版本升级（Qt 版本和 Chromium 版本绑定，
通常滞后上游一段时间），要么自己维护 CEF 版本。远程内容要限制来源，本地 HTML 也要防 XSS
打穿桥接 API（`QWebChannel` 暴露的每个槽都是攻击面）。

### 4.5 启动顺序与线程

WebEngine 的初始化和属性设置对启动顺序有要求（Qt 5 要求 `initialize()` 早于 `QApplication`，
Qt 6 改了，细节按所用版本核对），而 [`main.cpp`](https://github.com/terry-chao/tamias/blob/main/src/app/main.cpp)
已经在设 `AA_NativeWindows` 这类全局属性。落地前要在真实启动路径上验证，别只在 demo 里试。

### 4.6 和 Web 线的关系（真正的战略问题）

`web/` 已经是一套 React + WASM 的 UI（[WEB.md](WEB.md)）。所以「加 Chromium」有两种性质：

- **轻量**：给某几个功能开一个网页面板，桌面 UI 仍是 Qt。——按本文门禁走即可。
- **战略**：桌面 UI 逐步收敛到 React，Qt 只做壳。——那要一次定清楚是
  「Qt 壳 + WebEngine 面板」还是「CEF OSR 全自绘」，否则后面每加一个面板都要重新决策一次。

---

## 5. 三家对比（门禁通过后再看）

| 维度 | QtWebEngine | CEF | WebView2 |
|---|---|---|---|
| 许可 | LGPLv3 / 商业（动态链接 + 可替换库） | BSD-3 | 专有，免费分发 |
| 平台 | Win / Linux / macOS | Win / Linux / macOS | 仅 Windows |
| 与自研 RHI 合成 | 困难（走自己的 GPU 合成） | **可行**（OSR → 纹理） | 困难 |
| 体积 / 部署 | 百 MB 级，跟 Qt 走 | 自己打包，较大 | 依赖系统 WebView2 Runtime（或固定版本打包） |
| 更新节奏 | 跟 Qt 升级 | 自己定，自己跟 | 跟系统 Edge 走 |
| 集成工作量 | 低（Qt Widgets 直接放） | 高（生命周期、输入、渲染都得自己接） | 中 |
| 适合 | 网页在独立面板 | 网页要进三维视口 / 要精确控制 | Windows-only、想省事 |
| 不适合 | 视口内浮层 | 只想摆个网页面板 | 跨平台产品 |

### 5.1 什么时候才轮到 CEF（而不是 QtWebEngine / WebView2）

命中下面任意一条，CEF 才是答案；否则优先 QtWebEngine。

1. **网页要和三维画面同帧合成** —— 唯一真正「非 CEF 不可」的理由。CEF 提供 OSR：软渲染走
   `CefRenderHandler` 给位图，加速 OSR 走 `OnAcceleratedPaint` 给共享纹理（Windows 上 D3D11 /
   macOS 上 IOSurface），可以把浏览器帧当纹理喂进自研 RHI，做 HUD、贴合构件的标注、GIS 底图。
   QtWebEngine 不给你纹理句柄，这条路走不通。
   - **平台注意**：加速 OSR 在 Windows / macOS 可用，**Linux 侧基本只有软渲染逐帧拷贝**。
     Tamias 两个平台都要发，得按 Linux 的帧率底线设计——静态面板、地图没问题，全屏视频之类不行。
2. **要自己控制 Chromium 版本 / 安全补丁节奏**。QtWebEngine 的 Chromium 版本绑在 Qt 版本上，
   只能等 Qt 升级；CEF 想升就升（代价是回归测试自己扛）。
3. **需要 QtWebEngine 不暴露的 API**：自定义 scheme（`tamias://`）、请求拦截与自定义网络 / 代理 /
   证书、DevTools 远程调试、自定义 JS 绑定、多 profile / 离屏 profile——比如「每个插件一个独立
   profile」做隔离，这在插件 HTML UI 那条需求上是有分量的。
4. **不做 UI 嵌入，只要一个无头 HTML 渲染器**：报表（HTML 模板 → PDF）、缩略图 / 截图、批量渲染。
   这类需求和桌面壳无关，用 CEF（或 headless Chrome）当库即可；但如果只是偶尔出几张报表，
   先考虑服务端渲染，别为此在客户端塞一份 Chromium。
5. **不想背 Qt 依赖或许可路线**：纯 C++ 宿主（插件沙箱进程、非 Qt 的工具）没有 QtWebEngine 可用；
   或者想用 BSD-3 换掉 LGPLv3 那套合规工作。
6. **窗口形态需要精确控制**：CEF 的 windowed 模式本身就是一个原生子 HWND——对 Tamias 反而是
   「熟悉的地形」，现有原生子窗口的叠放与消息转发机制能复用；代价是它只能占一块矩形区域，
   不能和三维画面做 alpha 混合（要混合就回到第 1 条的 OSR）。

反过来记：**网页只要一个 dock 面板、合成不是问题 → 用 QtWebEngine，别上 CEF。**
CEF 的难点不在接 API，而在 IME、拖放、文件对话框、打印、崩溃恢复、子进程部署，
以及每个平台各一条构建流水线，全都要自己实现。

---

## 6. 推荐的推进路线

1. **阶段 0（现在）**：不引入。把「打开网页」做成 `openUrl` + 本地桥：
   app 起一个 loopback HTTP/WebSocket 服务，网页通过 `localhost` 调 app，网页跳回 app 用自定义协议
   （`tamias://`）。在线文档、登录、云平台跳转、模型库下载都能覆盖，且零 Chromium 负担。
2. **阶段 1**：抽象一个 `IWebSurface` 接口（`open(url)` / `注入数据` / `收消息`），先只实现外部浏览器版。
   以后加 WebEngine 不改业务代码。
3. **阶段 2**：命中门禁 + 通过第 7 节原型验收后，引入 **QtWebEngine**，形态限定为独立面板 / 标签页。
4. **阶段 3**：只有当「网页必须和三维视口同帧合成」成为硬需求时，才评估 **CEF + OSR**。

---

## 7. 立项前的最小原型（spike）验收清单

1. 同一主窗口里同时跑 `QWebEngineView` 面板 + Vulkan 视口：z-order、DPI 缩放、鼠标键盘、全屏、
   拖窗口、切标签页都正常（这是最容易翻车的一条）。
2. 面板加载本地 HTML，通过 `QWebChannel` 调 app：选中构件 → 网页高亮；网页 → `dispatch` 一条命令。
3. 干净机器部署：WebEngine 运行时文件被正确收集，安装包能启动，卸载不留残留。
4. 冷启动时间、内存增量的实测数字（对比当前版本）。
5. 离线 / 内网环境：断网时页面不卡 UI 线程，给出合理提示。
6. 许可评审：动态链接、可替换库、第三方声明文件随包分发。

第 1、2 条不过，后面的选型讨论都不用做。

---

## 8. 「什么时候需要」速查

```
需求里有网页
  ├─ 只是「打开我们的页面」 ──────► openUrl（不加库）
  ├─ 要在 app 里看，只读 ────────► 外部浏览器 + 本地桥（先别加）
  └─ 要在 app 里用，且和文档双向联动
        ├─ 独立面板就够 ─────────► 门禁通过 → QtWebEngine
        └─ 要和三维视口叠一起 ───► 门禁通过 → CEF + OSR（按周立项）
```

反过来记更省事：**只要还能用外部浏览器 + 本地桥糊过去，就不加。**
嵌入浏览器的代价不是磁盘上多一个 DLL，而是从此多背一条 Chromium 的安全更新线。

---

## 9. 候选功能：哪些功能真的会用到 CEF

「能不能用上 CEF」不取决于功能有多花哨，只取决于它有没有踩中第 5.1 节那六条。
下面三个是 Tamias 语境里成立的方向，按「非 CEF 不可」的程度排序。

### 9.1 首选：「场地模式」——真实地理底图和三维模型联动

**功能样子**：把模型放到真实场地上。底图显示地块、道路、周边建筑与地形，顶视图或三维视图里
两者按同一视角联动；能搜地址定位、量测、拾取坐标、给构件打地理锚点，导出带底图的场地截图 / 图纸。

**为什么非 CEF 不可**：

- 高德 / 百度 / 天地图 / Mapbox GL / 3D Tiles 这些**只有 JS SDK**，没有对等的可控 C++ SDK——
  Q1 通过。注意「自己下栅格瓦片贴纹理」是个伪替代：拿得到图片，拿不到矢量样式、POI 检索、
  路线和 3D Tiles。
- 底图要和三维画面**在同一帧里合成**（地图当背景或地面，视角跟着相机走）——Q2 通过。
  QtWebEngine 不给纹理句柄，只有 CEF 的加速 OSR 能把浏览器帧当纹理喂进自研 RHI。
- 双向交互天然成立——Q3 通过：视口点选 → 反算经纬度打点；地图上选地块 → 转工程坐标放构件。

**落地要点**：

- 坐标：工程坐标（m，相对原点）↔ CGCS2000 / WGS84，本地横轴墨卡托或接 PROJ。
  这一步和 CEF 无关，但没它这个功能就是错的。
- 相机同步：把视口中心点、缩放、方位角通过 JS 桥推给地图（`setCenter` / `setBearing`），
  反过来地图拖动时回推给相机——节流到十几帧每秒就够。
- **Linux 的软渲染 OSR 短板在这里不致命**：地图底图不需要 60fps，相机停稳后更新一次、
  拖动时降频重绘即可，纹理复用，帧率底线能守住。
- 成本与风险：OSR 合成周级工作；地图 SDK 有商用授权和联网 / 配额要求（天地图有免费额度，
  高德百度商用要授权，Mapbox 按量计费），离线内网交付得另配离线瓦片方案。

### 9.2 备选 A：插件 HTML UI 沙箱（战略价值最高）

**功能样子**：插件不只是往 Ribbon 塞按钮、弹宿主对话框，而是能自带一个 HTML 面板（表格、图表、
自定义表单、内嵌第三方网页），宿主负责渲染、隔离和能力授权。

**为什么用 CEF**：

- **每个插件一个独立 profile / 进程**做隔离——这一条 QtWebEngine 很难做（profile 基本全局），
  而在「插件来自第三方」的前提下它是刚需。
- 自定义 scheme（`tamias-plugin://<id>/`）加载本地资源 + 请求拦截做网络白名单，
  把「插件面板能访问什么」变成显式策略。
- 插件若要在三维视口里画 UI（视口内的浮动工具条、跟随构件的标签），只有 OSR 这条路。

**战略价值**：[插件设计](plugin/design.md) 现在的边界是「不给插件自建 HWND / 嵌入 WinForms」，
宿主只给窄对话框。HTML 沙箱是这条边界的自然延伸——能力仍然只有桥接出去的那几个，
但 UI 表现力放开，而且和 `web/` 的 React 技术栈打通。

### 9.3 备选 B：无头 HTML 报表 / 图纸导出（成本最低，适合当 CEF 的入场第一步）

**功能样子**：工程量清单、构件明细、图纸目录用 HTML 模板 + ECharts 排版，
一键导出 PDF / PNG，进图纸集或交付包。

**为什么用 CEF**：要漂亮的 CSS 排版和图表，`QTextDocument` 撑不住；又不想要求用户装浏览器。
CEF 的 headless 模式不需要窗口、不需要 Qt，`CefBrowserHost::PrintToPDF` 直接出 PDF。

**注意**：为了报表在客户端塞一份 Chromium（百 MB）是否值得，取决于产品形态——
私有化 / 离线交付划算，纯云端产品应该放服务端渲染。
但它的真正好处是**风险最低**：不碰窗口合成，可以先把 CEF 的构建、打包、许可、CI 跑通，
再决定要不要往 OSR 走。

### 9.4 建议顺序

| 顺序 | 做什么 | 为什么先做 |
|---|---|---|
| 1 | 9.3 无头报表 | 零合成风险，把 CEF 的工程链路（构建 / 打包 / 许可 / 升级）建立起来 |
| 2 | 9.2 插件 HTML 沙箱 | 用得上 profile 隔离和自定义 scheme，价值面向插件生态 |
| 3 | 9.1 场地模式 | 真正的差异化功能，但要等 OSR 合成能力验证过再上 |

一句话版：**想找一个非 CEF 不可的功能，就是「场地模式」；想低成本先把 CEF 引进来，就做无头报表。**
