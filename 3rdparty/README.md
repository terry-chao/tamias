# Third-party headers (vendored)

| File | Source |
|------|--------|
| `vk_mem_alloc.h` | [VulkanMemoryAllocator v3.1.0](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) |
| `rapidobj.hpp` | [rapidobj v1.0.1](https://github.com/guybrush77/rapidobj) (copied as `rapidobj/rapidobj.hpp` at configure time) |

Windows builds use these headers via the `msvc` preset. Linux may use vcpkg (`linux` preset).
