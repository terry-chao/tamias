# 性能分析（Tracy）

> 自研的 TimingPanel 面向用户：Release 也带、能录长段、导出 XML，回答「哪个操作慢」。Tracy 面向开发：只在 Debug/RelWithDebInfo 把客户端编进来，用它的独立界面看线程时间线、帧、锁和自定义曲线，回答「这 3ms 花在哪」。

**状态：** 一期（P0）与二期（Vulkan GPU zone）已落地 —— 依赖与开关、统一埋点宏、三条线程的命名与帧标记、第一批关键 zone 和曲线、GPU 侧的逐 pass 计时。内存、锁、采样还没做（见 §6）。

---

## 0. 一句话

埋点写一次：`TAMIAS_TIMING_SCOPE("名字", 类别)` 同时喂 TimingPanel 和 Tracy。开 Tracy 只要在 configure 时加 `-DTAMIAS_ENABLE_TRACY=ON`，Release / MSI 产物不受任何影响。

---

## 1. 打开与关闭

```powershell
$env:VULKAN_SDK = 'C:\VulkanSDK\<version>'
$env:VCPKG_ROOT = 'C:\dev\vcpkg'

cmake --preset msvc -DTAMIAS_ENABLE_TRACY=ON
cmake --build --preset relwithdebinfo --parallel
```

关掉就是把 `-DTAMIAS_ENABLE_TRACY=OFF` 再 configure 一次（同一个 build 目录即可，不用重建）。

规则：

- 客户端由 vcpkg 提供（`tracy` 0.13.1，feature `on-demand`），**只在 Debug/RelWithDebInfo 链接**，Release / MSI 里既没有符号也没有 DLL。这是 `cmake/TamiasTracy.cmake` 里的 `$<CONFIG:...>` 生成表达式决定的，不要改成无条件链接。
- `TRACY_ON_DEMAND` 是开的：没有 Profiler 连上来时客户端几乎不产生流量和开销，可以整天开着跑。
- `TracyClient.dll` 由 `tamias_copy_runtime_dlls()` 通过 `$<TARGET_RUNTIME_DLLS>` 自动拷到 `build/bin/<Config>/`，不用手抄。
- WASM 查看器强制关闭 Tracy（`EMSCRIPTEN` 下一律 OFF），`tamias::tracy` 仍然存在但什么都不做。
- **GPU 计时**只在 Vulkan 后端上工作，而且要求图形队列族的 `timestampValidBits != 0`（否则 `VulkanGpuTiming` 静默关掉）。物理设备有 `VK_EXT_calibrated_timestamps` 时会在建逻辑设备时打开它，Tracy 用它把 GPU 时钟对齐到 CPU 时钟；没有这个扩展时区间长度仍然准，但在时间线上的绝对位置会漂。

## 2. 跑一次

1. 启动 `build\bin\RelWithDebInfo\tamias.exe`。
2. 打开 Tracy 的界面（官方 release 包里的 `Tracy.exe`；**不要**用 vcpkg 的 `gui-tools` feature，它会连带拉 ImGui/GLFW/curl 一大串依赖，只为一个界面）。
3. 点 **Connect**：默认会自动发现局域网里广播的客户端；发现不了就手填 `127.0.0.1`，端口 `8086`。
4. 抓 5～30 秒（拖视口、拉一个 box、开一下 STEP 文件），然后 Stop。长会话只在 Profiler 那一侧吃内存，client 侧是固定环形缓冲。

## 3. 现在能看到什么

线程（Tracy 时间线按线程横向铺开）：

| 线程名 | 是什么 |
|---|---|
| `ui` | Qt 主线程：命令派发、求值、提交帧 |
| `render` | `RenderThread`：上传、剔除、录制、present |
| `tess` | `TessWorker`：后台三角化队列 |

帧：`render` 线程每次成功 present 打一个 frame mark，所以 Frame 视图里的帧时间就是真实的 present 间隔。多通道共享同一个 RenderThread 时，每个通道各算一帧。

曲线（Plots）：`draws`、`triangles`、`lod_requests`、`pending_tessellate`、`gpu_mesh_mb`。

已埋的 zone（颜色按类别：Command 蓝 / Modeling 橙 / Render 绿 / Ui 紫）：

| 区域 | zone |
|---|---|
| 命令 | `dispatch` / `feed_point` / `confirm`（名字=命令名，运行时字符串）、`undo`、`redo`、`open_file` |
| 建模 | `createGeom`、`evaluate_feature_model`、每个特征（`Boolean Cut`、`Extrude` …）、OCCT 的 `BRepMesh` / `extract_triangles` / `Shape::tessellate` / `STEPCAF ReadFile` |
| 三角化 | `tessellate`（`TessWorker` 队列里每个任务） |
| 渲染 | `draw_channel`、`upload_mesh`、`upload_texture` |
| UI | `submit_current_frame`（UI 线程构建 + 提交一帧） |
| GPU | `gpu.sky`、`gpu.grid`、`gpu.mesh`、`gpu.overlay`（每个视口一个 GPU 上下文行；Tracy 里用眼睛图标切换 GPU/CPU 行） |

GPU 行和 CPU 行并排，所以「这一帧是 CPU 提交慢还是显卡画得慢」能直接看出来：`draw_channel` 的 CPU 区段可能只有零点几毫秒，而 `gpu.mesh` 有好几毫秒 —— 那就是 GPU 顶到上限了。时间戳在 render pass 内写入（规范允许，只约束 query 索引不能越界），因为 tamias 一帧只有一个 render pass。

## 4. 新加埋点

```cpp
#include "engine/profile/timing_scope.h"

void hot_path() {
  TAMIAS_TIMING_SCOPE("my_hot_path", TimingCategory::Modeling);  // 名字必须是字面量
  ...
}

void per_object(const std::string& name) {
  TAMIAS_TIMING_SCOPE_DYNAMIC(name, TimingCategory::Ui);  // 运行时字符串
  ...
}
```

约定：

- **字面量名字用 `TAMIAS_TIMING_SCOPE`**：Tracy 把它放进 `constexpr SourceLocationData`，零分配。传运行时字符串会编译不过，这是故意的。
- **运行时字符串用 `TAMIAS_TIMING_SCOPE_DYNAMIC`**：名字会被拷进 Tracy 的缓冲，可以安全传临时 `std::string`；代价是每次多一次分配，所以别放进每帧几万次的内循环。
- 类别只影响配色和 TimingPanel 的芯片开关，不影响 Tracy 侧的开关。
- Tracy 没编进来时两条宏都退化成 `((void)0)`（TimingPanel 那一半照常工作）。
- 名字和曲线名在 Tracy 里是「存指针」的，自定义曲线请走 `TAMIAS_PROFILE_PLOT_INT/_FLOAT("字面量", v)`，不要传临时字符串。
- 线程名 / 帧标记：`profiling::set_thread_name("name")`、`TAMIAS_PROFILE_FRAME_MARK()`。

## 5. 和 TimingPanel 的关系

两者不互相替代，也不共用一个界面：

- TimingPanel（`Ctrl+Shift+T`）：录制开关、类别芯片、时间线、XML 导出。Release 也在，给 QA 和现场用。
- Tracy：开发机上的深度排查。GPU / 锁 / 采样 / 调用栈这些 TimingPanel 不会有。

埋点是同一条宏，所以加了 Tracy 埋点等于顺带给 TimingPanel 加了同样的区段——除非该类别在面板上被关掉。

## 6. 还没做（后续阶段）

- **OpenGL 后端的 GPU zone**：现在是空实现。要用得接 Tracy 的 OpenGL 上下文，并保证 context 在该线程 current（OpenGL 是每文档一个隔离线程）。
- **GPU 时间戳的可靠性**：规范只保证 timestamp 的写入顺序，不保证逐 draw 的精确归属（驱动可以并行/重排工作）。逐 pass 的量级可信，几十微秒级别的细分别当真。
- **锁**：`TimingSession::mutex_`、`RenderThread::mutex_` 换成 `TracyLockable` 就能看到争用。
- **内存**：需要在 EXE 里重载全局 `operator new/delete`，Qt 的分配噪声很大，建议只在专门排查时开。
- **采样 / 调用栈**：RelWithDebInfo（`/Zi /Ob1`）已经够用，Release 不适合采样。
- **CI**：目前 Tracy 只在本地开发构建里开。
