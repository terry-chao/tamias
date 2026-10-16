# glad（vendored 的 GL 入口点）

OpenGL 4.5 core 的函数指针、类型与枚举，全部由 [glad](https://github.com/Dav1dde/glad) 生成。
`src/engine/render/rhi/opengl/gl_loader.h` 只是把 glad 的符号映射回 `tamias::gl::` 那套名字，
再加上 WGL / GLX 的加载入口。

## 生成参数

用 glad 2.0.8（`pip install glad2`）生成，**带 `--reproducible`**：glad 只在带这个标志时用
包内自带的 Khronos XML，否则会去网上抓最新规范，产出就不可复现了。

```powershell
python -m glad --api "gl:core=4.5" `
  --extensions "GL_ARB_gl_spirv,GL_EXT_texture_filter_anisotropic" `
  --out-path 3rdparty/glad --reproducible --quiet c
```

- `gl:core=4.5`：后端 `wglCreateContextAttribsARB` / `glXCreateContextAttribsARB` 请求的就是
  4.5 Core（见 `docs/OPENGL.md`），版本要和它对上。
- `GL_ARB_gl_spirv`：`*.gl.spv` 走 `glShaderBinary` + `glSpecializeShaderARB`（4.6 才是无后缀
  的核心版），所以要这个扩展的入口与 token。
- `GL_EXT_texture_filter_anisotropic`：只为一个 token（`GL_TEXTURE_MAX_ANISOTROPY_EXT`）。

**加扩展要重新生成**：核心函数随版本自动带全，但扩展的入口与 token 只有列在 `--extensions`
里才有。重新生成后 `build/` 里的 `gl.c` 对象会重编，`gl_loader.h` 里加了新调用就补一行映射。

## 文件

| 文件 | 说明 |
|---|---|
| `include/glad/gl.h` | 声明：`glad_glFoo` 指针、`GLAD_GL_*` 标志、core 4.5 的类型与枚举 |
| `include/KHR/khrplatform.h` | glad 依赖的 Khronos 平台类型（`gl.h` 会 include 它） |
| `src/gl.c` | 实现：指针定义 + `gladLoadGL(loader)`，由 `tamias_rhi_opengl` 编译 |

## 许可

生成物的头部写着 `SPDX-License-Identifier: (WTFPL OR CC0-1.0) AND Apache-2.0`（见
`src/gl.c` 开头）。Khronos 的 `khrplatform.h` 是 Apache-2.0 / MIT 那套。都是宽松许可，
随分发带上这两份文件即可，不需要额外声明。
