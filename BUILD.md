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
# or: ctest --preset msvc-relwithdebinfo
# what is tested / missing: docs/TESTING.md
& .\build\bin\RelWithDebInfo\tamias.exe
```

First configure compiles OCCT from source (often 1–2 hours). Later configures reuse `vcpkg_installed/`. A [binary cache](https://learn.microsoft.com/en-us/vcpkg/users/binarycaching) avoids rebuilding on other machines.

Vulkan SDK is a **build-time** dependency only (headers + `dxc` for shaders). The loader is
resolved at **runtime** by volk (`3rdparty/volk.c`), so `tamias.exe` runs on machines without a
Vulkan runtime — the Vulkan backend reports a clear error and OpenGL stays available.

Dependencies resolved via:

- System Qt / Vulkan SDK
- vcpkg `opencascade` 7.9.3 (manifest)
- vcpkg `tacui` 0.1.0 (manifest, Windows x64) — from TacUI's own git registry, see `vcpkg-configuration.json`
- Vendored headers in `3rdparty/` (VMA, rapidobj, stb_truetype, volk)
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
| `TAMIAS_ENABLE_TRACY` | OFF | Link the Tracy profiler client in Debug/RelWithDebInfo only ([docs/PROFILING.md](docs/PROFILING.md)) |
| `TAMIAS_QT_PREFIX` | (empty) | Windows system Qt prefix; set by the `msvc` preset |
| `TAMIAS_BUILD_MSI` | ON (Windows) | Add the `tamias_msi` target |
| `TAMIAS_ENABLE_PCH` | ON | Precompiled headers (`src/pch.h`, plus Qt/gtest). **Off** on Windows `Ninja Multi-Config` (CMake's `.pch` is phony and would rebuild every file each build) |
| `TAMIAS_UNITY_BUILD` | OFF | Batch several `.cpp` into one TU (faster clean build, worse incremental) |
| `TAMIAS_COMPILER_CACHE` | ON | If `sccache` or `ccache` is on PATH, wrap `cl`/`c++` (helps **clean** rebuilds). Empty incremental builds should be `ninja: no work to do` without a cache. |
| `TAMIAS_ENABLE_MATH_MODULE` | OFF | C++20 module pilot: builds `tamias::math_module` (`tamias.math`) next to the header-only `tamias::math`. MSVC + GCC verified; Emscripten untested in CI. See [docs/DECISION-MODULES-MATH.md](docs/DECISION-MODULES-MATH.md) |

### OCCT (required, via vcpkg)

Do **not** set `OCCT_ROOT`. The `msvc` / `linux` presets load the vcpkg toolchain; `vcpkg.json` pins `opencascade` to 7.9.3 so IfcGeom can later link the same build.

IfcOpenShell **IfcParse** is fetched at configure time (tag `v0.8.0`) and compiled as a static lib. It does not link OCCT. Dump a spatial tree:

```powershell
cmake --build --preset debug --parallel --target tamias_ifc_dump
& .\build\bin\Debug\tamias_ifc_dump.exe .\assets\samples\spatial-tree.ifc
```

Opening a `.ifc` in the app shows the same tree. Geometry import (IfcGeom) is not wired yet.

On Windows, POST_BUILD copies Qt runtime (`windeployqt`) and OCCT / freetype DLLs into `build/bin/<Config>/` next to `tamias.exe`, so double-click / F5 works without Qt or OCCT on `PATH`.

### TacUI (Windows x64)

[TacUI](https://github.com/terry-chao/TacUI) is the GPU-rendered UI library behind the `tac`
interface layer. There are two ways in, and both hand out the same `TacUI::` target names:

**Local checkout** — the `msvc` preset sets `TAMIAS_TACUI_SOURCE_DIR` to `C:/dev/TacUI`. TacUI
is pulled in with `add_subdirectory(... EXCLUDE_FROM_ALL)`, so editing TacUI sources takes
effect on the next `cmake --build` with no reinstall step. Point the option at your checkout if
it lives elsewhere, or set it to empty to use the vcpkg copy — same as `TAMIAS_QT_PREFIX`.

**vcpkg package** — when `TAMIAS_TACUI_SOURCE_DIR` is empty, `find_package(TacUI CONFIG)`
resolves the copy vcpkg installed. TacUI's port is not in the builtin registry, so
`vcpkg-configuration.json` points vcpkg at TacUI's own **git registry** (port + `versions/`,
pinned by a baseline commit); `vcpkg.json` gates the dependency to `windows & x64`, matching the
port's `supports`, so the Linux and WASM presets never resolve it. An overlay port would *not*
give a live local loop — its portfile builds the `v0.1.0` tag tarball, not your working tree.

```cmake
find_package(TacUI CONFIG REQUIRED)
target_link_libraries(<target> PRIVATE TacUI::tacui_host)  # C++ side (core/rhi/text/platform)
target_link_libraries(<target> PRIVATE TacUI::tacui)       # C ABI only (tacui.dll)
```

TacUI's exported targets do not carry `cxx_std_17`, so the consumer has to set the C++ standard
itself — tamias already does, globally, at C++23. Nothing links TacUI yet, so the local
subproject is `EXCLUDE_FROM_ALL` and costs no build time; the dependency is in place for
[docs/TAC.md](docs/TAC.md) phase 5. `cmake/TamiasTacUI.cmake` sets `TAMIAS_TACUI_TARGET` to
`TacUI::tacui_host` from either source — that is what phase 5 has to link.

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
- Reference drawings (2D): PDF (needs `Qt6::Pdf`), DXF, SVG, images
