# Third-party headers (vendored)

| File | Source |
|------|--------|
| `vk_mem_alloc.h` | [VulkanMemoryAllocator v3.1.0](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) |
| `rapidobj.hpp` | [rapidobj v1.0.1](https://github.com/guybrush77/rapidobj) (copied as `rapidobj/rapidobj.hpp` at configure time) |
| `stb_truetype.h` | [stb v1.26](https://github.com/nothings/stb) — 字形度量 + 轮廓 + 光栅化（public domain；见 docs/TEXT.md §3.2）。**只用于受信任的字体文件**：该库不做越界检查 |

Windows builds use these headers via the `msvc` preset. Linux may use vcpkg (`linux` preset). OCCT is always from vcpkg (`opencascade` in `vcpkg.json`, pinned to 7.9.3). IfcOpenShell IfcParse is FetchContent from GitHub tag `v0.8.0` (LGPL-3.0), not vendored here.
