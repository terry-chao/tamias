# Tamias

C++23 / CMake / Qt 6 mesh viewer with a Vulkan-shaped RHI (Vulkan primary, OpenGL secondary).

## Status

| Milestone | Status |
|-----------|--------|
| M0 Skeleton (CMake, Qt tabs, Vulkan smoke) | Done |
| M1 Viewport + RenderThread | Done |
| M2 Mesh viewer (OBJ/GLB, shaded/wire, turntable) | Done |
| M3 Multi-doc share + BVH pick | Done |
| M4 OpenGL RHI + `Shape`/`IShapeOps` boundary | Done |
| M5 OCCT I/O (STEP/IGES/BREP tessellate) | Done |

## Build & run

See [BUILD.md](BUILD.md).

```powershell
cmake --preset msvc
cmake --build --preset relwithdebinfo --parallel
& .\build\msvc\bin\RelWithDebInfo\tamias.exe
```

## Layout

- `src/engine/rhi` — abstract RHI
- `src/engine/rhi_vulkan` — Vulkan backend
- `src/engine/rhi_opengl` — OpenGL backend (isolated thread per document)
- `src/engine/render` — `RenderThread` / `RenderChannel`
- `src/app` — Qt shell + native viewport
- `src/modeling` — `Shape` / `IShapeOps`; OCCT ops when `OCCT_ROOT` is set
