# Third-party headers (vendored)

| File | Source |
|------|--------|
| `vk_mem_alloc.h` | [VulkanMemoryAllocator v3.1.0](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) |
| `rapidobj.hpp` | [rapidobj v1.0.1](https://github.com/guybrush77/rapidobj) (copied as `rapidobj/rapidobj.hpp` at configure time) |
| `stb_truetype.h` | [stb v1.26](https://github.com/nothings/stb) — 字形度量 + 轮廓 + 光栅化（public domain；见 docs/TEXT.md §3.2）。**只用于受信任的字体文件**：该库不做越界检查 |
| `volk.h` / `volk.c` | [volk v1.4.321.0](https://github.com/zeux/volk) — Vulkan 元加载器（MIT）。运行时取 `vulkan-1.dll` / `libvulkan.so.1`，没装 runtime 也能启动（见 docs/RENDERING.md §5）。tag 取 `vulkan-sdk-1.4.321.0`：**不高于**本机 Vulkan 头版本，避免引用 SDK 里还没有的符号 |

Windows builds use these headers via the `msvc` preset. Linux may use vcpkg (`linux` preset). OCCT is always from vcpkg (`opencascade` in `vcpkg.json`, pinned to 7.9.3). IfcOpenShell IfcParse is FetchContent from GitHub tag `v0.8.0` (LGPL-3.0), not vendored here.
