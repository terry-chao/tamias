# 第 2 章　构建与运行：把源码变成能跑的程序

> 本章目标：在你自己机器上编译出 Tamias 并运行。这是 C++ 3D 开发的第一道真实门槛——项目还没写，先把「怎么把它搭起来」搞明白。

## 2.1 为什么要先搞定构建

C++ 3D 项目几乎都依赖一堆第三方库（图形 API、几何内核、UI 框架）。构建系统负责：找库 → 编译 → 链接 → 拷贝运行库。Tamias 用 **CMake**，并配好了一套 **CMake Presets**——这是 CMake 3.21+ 的官方机制，把「常用配置」存成命名预设，一行命令就能用。

## 2.2 需要装什么（Windows 推荐路径）

| 依赖 | 说明 |
|---|---|
| **Visual Studio 2022/2026**（C++ 桌面开发） | 编译器 + 调试器 |
| **Qt 6**（测试过 6.11.1 `msvc2022_64`） | 桌面 UI 框架 |
| **Vulkan SDK** | 主渲染后端 + shader 编译 |
| **vcpkg**（放在 `C:\dev\vcpkg`） | 包管理器，装 OCCT |
| **CMake 3.24+** | 构建系统 |

> ⚠️ 两个常见坑：vcpkg **不要**用 Visual Studio 自带的那份；CMakePresets 里的 `TAMIAS_QT_PREFIX` 如果 Qt 路径不同要改。

Linux 用 `linux` preset，装 Qt6/Vulkan 即可，OCCT 仍走 vcpkg。详细步骤见 [BUILD.md](https://github.com/terry-chao/tamias/blob/main/BUILD.md)。

## 2.3 第一次构建（记住这四步）

```powershell
$env:VULKAN_SDK = 'C:\VulkanSDK\<你的版本>'
$env:VCPKG_ROOT = 'C:\dev\vcpkg'
$env:Path = "$env:VULKAN_SDK\Bin;C:\Qt\6.11.1\msvc2022_64\bin;$env:Path"

cmake --preset msvc                        # 1. 配置：找依赖、生成工程
cmake --build --preset relwithdebinfo --parallel   # 2. 编译
ctest --test-dir build -C RelWithDebInfo --output-on-failure   # 3. 跑测试
& .\build\bin\RelWithDebInfo\tamias.exe    # 4. 运行
```

**第一次配置要等很久（1–2 小时）**，因为 OCCT 是从源码编译的。之后会复用 `vcpkg_installed/`，再配就快得多。这不是卡住了。

## 2.4 常见的「我以为坏了」时刻

| 现象 | 原因 | 处理 |
|---|---|---|
| 配置时报 vcpkg toolchain 没加载 | 旧 `build/` 里没有工具链缓存 | 删掉 `build/` 重新 `cmake --preset msvc` |
| `OCCT_ROOT` 相关报错 | 曾手动指定过 OCCT | 不要设置 `OCCT_ROOT`，统一走 vcpkg 的 `opencascade` |
| 运行后黑屏/崩 | GPU 驱动不支持 Vulkan | 设置里切 OpenGL 后端 |
| 编译很慢 | OCCT 全量编译 | 开 binary cache，或减少并行度避免内存爆 |

## 2.5 CMake 选项速读（新手版）

打开根目录 [`CMakeLists.txt`](https://github.com/terry-chao/tamias/blob/main/CMakeLists.txt) 能看到一组 `TAMIAS_ENABLE_*` 开关，它们是学 CMake 的好教材：

| 选项 | 默认 | 意思 |
|---|---|---|
| `TAMIAS_ENABLE_VULKAN_BACKEND` | ON | 编不编 Vulkan 渲染后端 |
| `TAMIAS_ENABLE_OPENGL_BACKEND` | ON | 编不编 OpenGL 后端 |
| `TAMIAS_ENABLE_WEBGL_BACKEND` | OFF（WASM 时 ON） | 编不编浏览器 WebGL 后端 |
| `TAMIAS_ENABLE_OCCT` | ON | 编不编几何内核（WASM 时 OFF） |
| `TAMIAS_BUILD_TESTS` | ON | 编不编单元测试 |

## 2.6 动手练习

1. 完成一次完整构建并启动 Tamias（能打开窗口就算过关）。
2. 运行 `ctest`，看有几个测试通过。测试文件在 [`tests`](https://github.com/terry-chao/tamias/tree/main/tests)。
3. 试着把 `TAMIAS_ENABLE_OPENGL_BACKEND=OFF` 配置一次，再在设置里观察：OpenGL 选项是不是消失了？

## 延伸阅读

- [BUILD.md](https://github.com/terry-chao/tamias/blob/main/BUILD.md)：完整构建指南（含 MSI 打包）
- [CMakePresets.json](https://github.com/terry-chao/tamias/blob/main/CMakePresets.json)：所有预设定义
- [vcpkg.json](https://github.com/terry-chao/tamias/blob/main/vcpkg.json)：依赖清单（可以看到 OCCT 被钉在 7.9.3）

下一章：[界面与交互](03-ui-and-interaction.md)
