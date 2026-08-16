# Tamias Build Guide

Supports **Windows x64** and **Linux x64** (X11/XCB). C++23, CMake Presets.

## Recommended Windows path (system Qt + Vulkan SDK)

Prerequisites:

- Visual Studio 2022/2026 with C++ desktop workload
- [Qt 6](https://www.qt.io/) (tested: 6.11.1 `msvc2022_64`)
- [Vulkan SDK](https://vulkan.lunarg.com/)
- CMake 3.24+

```powershell
$env:VULKAN_SDK = 'C:\VulkanSDK\<version>'
$env:Path = "$env:VULKAN_SDK\Bin;C:\Qt\6.11.1\msvc2022_64\bin;$env:Path"

# Edit CMakePresets.json CMAKE_PREFIX_PATH if your Qt path differs.
cmake --preset msvc
cmake --build --preset relwithdebinfo --parallel
ctest --test-dir build -C RelWithDebInfo --output-on-failure
& .\build\bin\RelWithDebInfo\tamias.exe
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
| `TAMIAS_ENABLE_OPENGL_BACKEND` | ON | OpenGL RHI (isolated thread per document) |
| `TAMIAS_ENABLE_OCCT` | ON | OCCT ShapeOps when `OCCT_ROOT` is set |
| `TAMIAS_BUILD_TESTS` | ON | gtest targets |
| `TAMIAS_USE_FETCHCONTENT` | ON | Fetch gtest when not found |

### OCCT (optional)

Set `OCCT_ROOT` to an Open CASCADE install (tested: 7.9.x Windows `vc14-64` layout with `inc/`, `cmake/`, `win64/vc14/{bin,bind,lib,libd}`):

```powershell
$env:OCCT_ROOT = 'C:\path\to\opencascade-7.9.3-vc14-64'
```

When `OCCT_ROOT` is present, Tamias builds `OcctShapeOps` and can open STEP/IGES/BREP. Without it, the rest of the app still builds.

On Windows, POST_BUILD copies Qt runtime (`windeployqt`) and, when OCCT is enabled, toolkit plus required 3rdparty DLLs into `build/bin/<Config>/` next to `tamias.exe`, so double-click / F5 works without Qt or OCCT on `PATH`.

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
- STEP / IGES / BREP via OCCT (when built with `OCCT_ROOT`)
