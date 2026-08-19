# Tamias Build Guide

Supports **Windows x64** and **Linux x64** (X11/XCB). C++23, CMake Presets.

## Recommended Windows path (system Qt + Vulkan SDK + vcpkg OCCT)

Prerequisites:

- Visual Studio 2022/2026 with C++ desktop workload
- [Qt 6](https://www.qt.io/) (tested: 6.11.1 `msvc2022_64`)
- [Vulkan SDK](https://vulkan.lunarg.com/)
- [vcpkg](https://vcpkg.io/) at `C:\dev\vcpkg` (`VCPKG_ROOT`; Open CASCADE comes from `vcpkg.json`, pinned to 7.9.3). Do **not** use the copy bundled with Visual Studio.
- CMake 3.24+

```powershell
$env:VULKAN_SDK = 'C:\VulkanSDK\<version>'
$env:VCPKG_ROOT = 'C:\dev\vcpkg'
$env:Path = "$env:VULKAN_SDK\Bin;C:\Qt\6.11.1\msvc2022_64\bin;$env:Path"

# Edit CMakePresets.json TAMIAS_QT_PREFIX if your Qt path differs.
# If this tree was previously configured with OCCT_ROOT, delete build/ first.
cmake --preset msvc
cmake --build --preset relwithdebinfo --parallel
ctest --test-dir build -C RelWithDebInfo --output-on-failure
& .\build\bin\RelWithDebInfo\tamias.exe
```

First configure compiles OCCT from source (often 1–2 hours). Later configures reuse `vcpkg_installed/`. A [binary cache](https://learn.microsoft.com/en-us/vcpkg/users/binarycaching) avoids rebuilding on other machines.

Dependencies resolved via:

- System Qt / Vulkan SDK
- vcpkg `opencascade` 7.9.3 (manifest)
- Vendored headers in `3rdparty/` (VMA, rapidobj)
- FetchContent zip for GoogleTest (tests only)

## Linux

Use the `linux` preset with vcpkg (`linux-desktop` feature) or install Qt6/Vulkan via distro packages and adjust `CMAKE_PREFIX_PATH`. OCCT is still taken from vcpkg.

## Options

| CMake option | Default | Meaning |
|---|---|---|
| `TAMIAS_ENABLE_VULKAN_BACKEND` | ON | Vulkan RHI (primary) |
| `TAMIAS_ENABLE_OPENGL_BACKEND` | ON | OpenGL RHI (isolated thread per document) |
| `TAMIAS_BUILD_TESTS` | ON | gtest targets |
| `TAMIAS_USE_FETCHCONTENT` | ON | Fetch gtest when not found |
| `TAMIAS_QT_PREFIX` | (empty) | Windows system Qt prefix; set by the `msvc` preset |
| `TAMIAS_BUILD_MSI` | ON (Windows) | Add the `tamias_msi` target |

### OCCT (required, via vcpkg)

Do **not** set `OCCT_ROOT`. The `msvc` / `linux` presets load the vcpkg toolchain; `vcpkg.json` pins `opencascade` to 7.9.3 so IfcGeom can later link the same build.

IfcOpenShell **IfcParse** is fetched at configure time (tag `v0.8.0`) and compiled as a static lib. It does not link OCCT. Dump a spatial tree:

```powershell
cmake --build --preset debug --parallel --target tamias_ifc_dump
& .\build\bin\Debug\tamias_ifc_dump.exe .\assets\samples\spatial-tree.ifc
```

Opening a `.ifc` in the app shows the same tree. Geometry import (IfcGeom) is not wired yet.

On Windows, POST_BUILD copies Qt runtime (`windeployqt`) and OCCT / freetype DLLs into `build/bin/<Config>/` next to `tamias.exe`, so double-click / F5 works without Qt or OCCT on `PATH`.

## Windows MSI

Requires the [.NET SDK](https://dotnet.microsoft.com/download) (for the WiX tool). First-time `tamias_msi` downloads WiX 5 and `WixToolset.UI.wixext`. Use **Release** (not Debug). Load the VS developer environment first:

```powershell
$env:VULKAN_SDK = 'C:\VulkanSDK\<version>'
$env:VCPKG_ROOT = 'C:\dev\vcpkg'
$env:Path = "$env:VULKAN_SDK\Bin;C:\Qt\6.11.1\msvc2022_64\bin;$env:Path"

cmd /c "call `"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`" && cmake --preset msvc && cmake --build --preset msi --parallel"
```

In Cursor / VS Code: **Terminal → Run Task… → Tamias: 打包 MSI** (does not switch your daily Debug preset).

Output: `build/package/Tamias-<version>-win64.msi`. It installs to `C:\Program Files\Tamias\`, adds a Start Menu shortcut, and bundles Qt / OCCT / the VC++ runtime next to `tamias.exe`. The machine still needs a GPU driver with Vulkan (or switch the app to OpenGL).

`cmake --install build --config Release --prefix <dir>` stages the same payload without building an MSI.

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
- STEP / IGES / BREP via OCCT
- IFC spatial structure via IfcOpenShell IfcParse (geometry not imported yet)
