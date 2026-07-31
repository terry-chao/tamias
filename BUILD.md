# Tamias Build Guide

Supports **Windows x64** and **Linux x64** (X11/XCB). C++23, CMake Presets.

## Recommended Windows path (system Qt + Vulkan SDK)

Prerequisites:

- Visual Studio 2022/2026 with C++ desktop workload
- [Qt 6](https://www.qt.io/) (tested: 6.8.3 `msvc2022_64`)
- [Vulkan SDK](https://vulkan.lunarg.com/)
- CMake 3.24+

```powershell
$env:VULKAN_SDK = 'C:\VulkanSDK\<version>'
$env:Path = "$env:VULKAN_SDK\Bin;C:\Qt\6.8.3\msvc2022_64\bin;$env:Path"

# Edit CMakePresets.json CMAKE_PREFIX_PATH if your Qt path differs.
cmake --preset msvc
cmake --build --preset relwithdebinfo --parallel
ctest --test-dir build/msvc -C RelWithDebInfo --output-on-failure
& .\build\msvc\bin\RelWithDebInfo\tamias.exe
```

Dependencies resolved via:

- System Qt / Vulkan SDK
- Vendored headers in `3rdparty/` (VMA, rapidobj)
- FetchContent zip for GoogleTest (tests only)

## Linux

Use the `linux` preset with vcpkg (`linux-desktop` feature) or install Qt6/Vulkan via distro packages and adjust `CMAKE_PREFIX_PATH`.

## Options

| CMake option | Default | Meaning |
|---|---|---|
| `TAMIAS_ENABLE_VULKAN_BACKEND` | ON | Vulkan RHI (primary) |
| `TAMIAS_ENABLE_OPENGL_BACKEND` | ON | OpenGL stub registration (isolation policy) |
| `TAMIAS_BUILD_TESTS` | ON | gtest targets |
| `TAMIAS_USE_FETCHCONTENT` | ON | Fetch gtest when not found |

## Controls

- **Left drag**: orbit
- **Left click** (no drag): pick / select
- **Right / Middle drag**: pan
- **Wheel**: dolly
- **F**: frame all

## Formats

- OBJ (rapidobj)
- GLB (minimal built-in loader)
- ASCII `.gltf` not yet supported — convert to `.glb` or `.obj`
