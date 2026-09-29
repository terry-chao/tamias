# 决策：C++20 模块试点（tamias::math）

> 回应「这个项目引入 C++20 模块靠谱吗」。结论先行：**靠谱，但先只在 `tamias::math` 试，且默认关闭。**
> 试点已经在 MSVC / GCC / Emscripten 三条线上编译并跑通，取值与头文件路径逐项一致；
> 头文件仍是唯一事实来源和对外接口，模块只是一层打包。真正全面迁移要等下面第 5 节的
> 前置条件满足。

---

## 0. 一句话结论

| 问题 | 结论 |
|---|---|
| 工具链能不能编？ | 能。MSVC 19.51 / GCC 15.2 / emcc 6.0.8 三线都过（含 CMake 集成与跨 TU 混编） |
| 现在全面迁？ | 不。收益面小（自有代码仅 255 `.cpp` + 308 `.h`，已上 PCH/unity），风险面大（三平台 + 无 C++ CI + Qt moc） |
| 那现在做什么？ | `tamias::math` 试点 + 独立脚手架 + 最小 CI；开关默认 OFF，随时可退 |
| 当前阻塞？ | Windows 的 `Ninja Multi-Config` 构建里，**重建**模块消费者 target 时 Ninja 会断言崩溃并挂住（见第 4 节第 2 条）。所以开关保持 OFF，只在需要时手动开、用完关 |
| 最大收益在哪？ | 不在编译时间，而在**模块边界**：把「谁能看见谁」从约定变成编译器强制 |

---

## 1. 为什么选 `tamias::math`

`src/engine/math/` 只有 4 个头文件（`math.h` 378 行、`camera.h` 145、`grid.h` 47、`aabb2.h` 20），
纯 header-only、全是 `inline`/`constexpr`、零宏、零模板、零第三方依赖，且是叶子库——
64 个 TU 用它的头文件，反过来它不依赖任何人。这是「验证工具链」而非「验证收益」的理想样本。

## 2. 怎么接的

| 文件 | 作用 |
|---|---|
| `src/engine/math/tamias.math.cppm` | 模块接口单元。`module;` 里放标准库头，`export { #include ... }` 把四个头文件的声明导入模块并导出——**不复制实现**，避免两份事实来源 |
| `src/engine/math/CMakeLists.txt` | `TAMIAS_ENABLE_MATH_MODULE`（默认 OFF）打开后多出 `tamias::math_module` 这个 target |
| `tests/math_module/` | 对比探针：`header_side.cpp` 只 include，`module_side.cpp` 只 import，`probe_body.inc` 是共享计算体 |
| `tests/math_module_parity_tests.cpp` | gtest：模块值与手算基准一致、两侧逐项一致、探针槽位没有漏填 |
| `tests/math_module/standalone/CMakeLists.txt` | 独立脚手架，不拉 vcpkg/Qt/OCCT，CI 与本地排查用 |
| `.github/workflows/math-module-pilot.yml` | 最小 CI：windows-msvc + linux-gcc14 |

日常用法：

```powershell
# 真实工程里打开模块 target
cmake --preset msvc -DTAMIAS_ENABLE_MATH_MODULE=ON

# 或者只跑试点本身（秒级 configure，不需要 Qt/OCCT/vcpkg）
cmake -S tests/math_module/standalone -B build/math-module -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/math-module
ctest --test-dir build/math-module --output-on-failure
```

试点测试单独成一个 `tamias_math_module_tests` target，而不是塞进 `tamias_tests`：
主测试 target 挂着 PCH、unity、目标文件缓存等既有加速手段，混在一起会污染实验结论。

### 2.1 扩展名为什么是 `.cppm`

C++ 标准不规定模块文件的扩展名，`.ixx` / `.cppm` 纯粹是工具链约定，对产物没有任何影响。
两个都不是「平台无关」的——各大驱动各有各的盲区（以下为本机实测）：

| 驱动 | `.ixx` | `.cppm` | `.mpp` |
|---|---|---|---|
| MSVC `cl` | 直接出 obj | **静默忽略**：D9024/D9027/D9021，退出码 0、不产出 obj | 同 `.cppm` |
| Clang（emcc 的 clang++） | 当链接输入忽略，无 obj | 直接出 obj | 无 obj |
| GCC | 直接出 obj | 直接出 obj | 当链接输入忽略，无 obj |
| CMake | 认（3.21 起） | 认（3.21 起） | 认 |
| Visual Studio / MSBuild | 映射 `CompileAsCppModule` | 映射 `CompileAsCppModule` | — |

也就是说：走 CMake 时两者等价（CMake 会替 MSVC 补 `/TP /interface`、替 clang 补 `-x c++-module`），
落地差异只出现在「有人绕过构建系统直接把文件喂给编译器」的时候。

选 `.cppm` 的理由是**跟生态对齐**——出货实物里：

| 项目 | 用的扩展名 |
|---|---|
| MSVC STL | `std.ixx`、`std.compat.ixx` |
| libc++（LLVM） | `std.cppm`、`std.compat.cppm` |
| Vulkan-Headers（Khronos） | `vulkan.cppm`、`vulkan_video.cppm`（安装规则写死 `*.cppm`） |
| GLM | `glm.cppm` |
| Emscripten 自带测试 | `hello_world.cppm` |
| fastgltf（本仓库的依赖） | `fastgltf.ixx` |

Clang 已经是本项目的目标编译器之一（Emscripten），而 clang 原生认 `.cppm` 不认 `.ixx`；
加上消费的跨平台库（libc++ / Vulkan-Headers / GLM）都用 `.cppm`，所以试点选它。
反过来说，如果哪天 Windows/MSVC 变成唯一战场、或者要频繁手敲 `cl` 做实验，换回 `.ixx` 只是一行文件名。

### 2.2 为什么没有显式 `export` 列表

`export { #include ... }` 导出的是头文件里**声明的实体**（类型 / 函数 / 常量），不是"头文件"——
头文件本身不是可导出的东西。当前导出面就是 `namespace tamias` 里这 38 个名字：
`Vec2` `Vec3` `Mat4` `Aabb` `Aabb2` `Plane` `Frustum` `Ray` `TurntableCamera` `dot` `cross`
`length` `normalize` `operator*(Mat4,Mat4)` `operator*(Mat4,Vec3)` `invert_affine` `translate`
`scale` `rotate_x` `rotate_y` `segment_model` `perspective` `ortho` `look_at` `transform_aabb`
`intersect_aabb` `intersect_triangle` `intersect_segment` `snap_to_grid_xz` `snap_to_grid_xz_if_near`
`grid_snap_world_radius` `is_on_grid_xz`，以及常量 `kGridMinorSpacing` `kGridMajorSpacing`
`kGridSnapPixels` `kGridSnapRadiusFactor` `kHalfPi` `kSketchPickRadius`。

代价是**导出集不可控**：头文件声明什么就导出什么，没有筛选余地。

想「显式列表 + 代码继续留在头文件」的两种写法，实测都不成立：

| 变体 | MSVC | GCC |
|---|---|---|
| 头文件 include 在模块 purview 内，再 `export using tamias::Vec3;` 等 | 模块编过，但导出无效，消费者 `C2039` | 直接报错：`exporting 'struct demo::Widget' that does not have external linkage` |
| 头文件 include 在 global module fragment，再 `export using` | 模块编过，导出集为空 | 模块编过，导出集为空 |

所以结论是：**要显式 `export` 列表，代码就必须写在模块单元里**。想同时保留头文件路径，只能二选一：

* 头文件与模块并存 → 转场期两份代码，靠第 2 节的 parity 测试防漂移；
* 头文件退化成 `import tamias.math;` 的转发层 → 头文件路径从此依赖模块，64 个 include 它的 TU
  所在 target 都要打开扫描，等于一次性迁移。

试点阶段保留 façade：它能验证工具链，但拿不到封装收益（不能藏内部实现、宏不跨模块、
内部链接的 `kSketchPickRadius` 这类实体本来就不该出现在导出面里）。真正的迁移要等第 5 节的条件满足。

## 3. 实测结果

环境：CMake 4.2.1 · MSVC 19.51（VS 18 / 14.51）· GCC 15.2（MSYS2 UCRT）· emcc 6.0.8。
测试内容：模块编译 → BMI 传播 → 消费者 `import` → 与头文件路径逐项比对。

| 组合 | 结果 |
|---|---|
| MSVC + Ninja Multi-Config | 通过（3/3） |
| MSVC + `CMAKE_CXX_COMPILER_LAUNCHER`（`cl_utf8_launcher.exe`） | 通过，dyndep 扫描没被启动器破坏 |
| MSVC + `target_precompile_headers` 与模块 target 并存 | 通过 |
| GCC 15.2 + Ninja（产出 `tamias.math.gcm`） | 通过（3/3） |
| Emscripten 6.0.8 + CMake（`emscan-deps` 扫描） | 通过（3/3，node 跑 wasm 输出一致） |
| 一个可执行文件里同时含 header 侧 TU 与 module 侧 TU | 链接、运行都正常 |
| 真实工程：`cmake --preset msvc -DTAMIAS_ENABLE_MATH_MODULE=ON` → `--target tamias_math_module_tests` | 通过（3/3，带着项目自己的 `cl_utf8_launcher`、`/MP`、`/FS`、PCH 设置） |

`import std;`（标准库模块）三条线都不可用：MSVC 报找不到模块 `std`，GCC 需要预建
`gcm.cache/std.gcm`，emcc 直接拒绝 `-fmodules-ts`。所以模块目前只能用来包**自己写的代码**。

## 4. 边界与坑

1. **CMP0155：`cmake_minimum_required(VERSION 3.24)` 会让消费侧不扫描。** 这是试点里最花时间的一条。
   本仓库最低要求 3.24，而负责「扫描 CU 里的 `import`」的 CMP0155 是 3.28 引入的，于是 CMake 仍然
   假设源码不 import 模块：`module_side.cpp` 用 **unscanned** 规则编译，拿不到 BMI 引用，报
   `C2230 无法找到模块 "tamias.math"`。解法是在需要的 target 上显式
   `set_property(TARGET ... PROPERTY CXX_SCAN_FOR_MODULES ON)`（见 `tests/CMakeLists.txt`）。
   只有 `CXX_MODULES` 文件集里的模块单元本身不受影响——它无论如何都会被扫描，所以「模块编得出来、
   用的人编不过」是这种配置的典型症状。整体迁移时要付的账是：全项目每个 TU 多一遍扫描。
2. **Windows `Ninja Multi-Config` 上的 Ninja 断言（当前阻塞项）。** 真实工程里对模块消费者 target 做
   **重建**（改过数学头文件、或 CMake 重新生成之后）会在 `Generating CXX dyndep file
   tests\...\CXX.dd` 这一步崩：

   ```
   Assertion failed: edge && !edge->outputs_ready(), file ...\src\build.cc, line 435
   ```

   随后 `ninja` 挂住不退出（用 `-j1` 可稳定复现，多核时随机）。清空整个 `build/` 从头构建则是好的，
   本机验证过的那次也是这么过的。相同图结构（模块 target + 跨 target import）在 `tests/math_module/
   standalone` 的脚手架里复现不出来——差别在于真实工程是同一个 Ninja 图里混着扫描/不扫描的 target，
   且模块 target 与消费者分处不同目录。**这更像是 Ninja 1.13.2 处理「dyndep 声明的隐式输出
   （模块 BMI）由已完成的边产出」时的缺陷，而不是试点代码的问题**，但尚未定位到确切触发条件。

   现状：`TAMIAS_ENABLE_MATH_MODULE` 保持默认 OFF，别进日常构建。临时救急可以删掉
   `build/src/engine/math/CMakeFiles/tamias_math_module.dir` 与
   `build/tests/CMakeFiles/tamias_math_module_tests.dir` 再构建（验证过一次可行），但这是绕过不是修复。
   下一步要试的方向：换 Ninja 版本、改单配置 `Ninja` 生成器、或直接给 Ninja / CMake 报 issue。
3. **一个 TU 只能选一种**。同一个 TU 里既 `#include` 头文件又 `import` 同一个模块，会让同名实体
   同时挂在全局模块和具名模块上。因此试点把两侧拆成两个 TU；迁移任何库时这条都要写成规范。
4. **`export` 块里的内部链接实体**。`math.h` 里 `constexpr float kSketchPickRadius` 是隐式内部链接，
   放进 `export { }` 属于踩线行为。目前三个编译器都放过，但**下一步若扩大试点，先把它改成
   `inline constexpr`**（与 `grid.h`/`camera.h` 的写法一致）。
5. **Emscripten 冷缓存**。`EM_CACHE` 全新时 `emscan-deps` 可能在扫描阶段报 `'cstdio' file not found`
   （它不会替你先装 sysroot）。先跑一次普通 emcc 编译预热即可；真实 wasm 目标尚未进 CI。
6. **CMake 的文件集基目录**。`FILE_SET CXX_MODULES` 要求模块文件落在 `BASE_DIRS` 内，默认是当前
   `CMakeLists.txt` 所在目录；跨目录引用（脚手架那套）要显式写 `BASE_DIRS`。
7. **MSVC 需要 `/utf-8`**。这个项目源码里有中文注释；脚手架忘了 `/utf-8` 会得到满屏语法错误
   （不是模块的锅）。
8. **Qt 那层暂时别碰**。`AUTOMOC` 生成的 moc TU 不认识模块，`Q_OBJECT` 类模块化要手工接线；
   `src/app` 维持现状。
9. **依赖栈没有模块**。`vcpkg_installed` 里 OCCT / Qt / spdlog / gtest 等一个 `.ixx/.cppm` 都没有，
   模块边界只能靠 global module fragment 包自己的代码。

## 5. 什么条件下推进到下一步

按优先级：

0. 先解决第 4 节第 2 条的 Ninja 断言（换 Ninja 版本、改单配置生成器、或上游修复）。在这之前，
   `TAMIAS_ENABLE_MATH_MODULE` 只当实验开关，不进日常构建。
1. 手动 `workflow_dispatch` 跑一次 `math-module-pilot`，确认两个 job 都绿（本文件是在无网络环境下写的，
   这份 CI 尚未真实跑过）。真实工程的 `TAMIAS_ENABLE_MATH_MODULE=ON` 构建 + 测试已经在本机验证过，
   CI 只是把它固化下来。
2. 把 `kSketchPickRadius` 改成 `inline constexpr`，并在本文件里记一笔。
3. 给一个**内部依赖更多**的库做第二个样本（例如 `tamias::base`），验证「模块 import 模块」的构建图。
4. 只有当「模块边界带来的收益」值得时，才考虑把模块当作主接口、头文件退化成转发层——那时
   才会对编译时间产生实质影响，也才需要重新评估 PCH / unity / 编译器缓存。

## 6. 回退

`TAMIAS_ENABLE_MATH_MODULE` 默认 OFF，关掉即回到纯头文件路径；试点新增的 target 与测试与主线构建
完全解耦，删除 `tamias.math.cppm`、`tests/math_module/`、`standalone/` 与那个 workflow 即可完全复原。

---

相关：[BUILD.md](../BUILD.md) · [ARCHITECTURE.md](ARCHITECTURE.md)
