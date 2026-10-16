#pragma once

// OpenGL 4.5 入口点：函数指针（以及 4.5 core 需要的类型与枚举）全部由 vendored 的 glad
// 提供，见 3rdparty/glad —— 生成参数、复现命令与许可写在那个目录的 README.md 里。
//
// 调用点直接用 glad 展开的 GL 名字：glad 的 gl.h 里有
// `#define glBufferData glad_glBufferData`，所以 opengl_device.cpp 里就是 `glBufferData(...)`，
// 没有一层中转表。这个文件只留三样东西：
//
//   1. 加载入口 load_procs() / get_proc()：gladLoadGL 要一个「名字 → 地址」的回调，
//      这里把平台的 wglGetProcAddress / glXGetProcAddressARB 接上去（含 Windows 那几个
//      哨兵值与 opengl32.dll 兜底）；
//   2. WGL 的两个扩展入口：建 4.5 Core 上下文要用，且只能在有 current 上下文时取；
//   3. GLX 侧只多一个 glXCreateContextAttribsARB，在 opengl_device.cpp 里就地取。
//
// 能力看 glad 的 GLAD_GL_* 标志（例：SPIR-V 通路要 GLAD_GL_ARB_gl_spirv）。4.5 上还没有
// 核心版 glSpecializeShader（4.6 才转正），所以调用点写的是 glSpecializeShaderARB。

#include <glad/gl.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>  // WGL 入口点要用 HDC / HGLRC / WINAPI
#else
#include <GL/glx.h>  // glXGetProcAddressARB
#endif

namespace tamias::gl {

// 必须在「有一个 current 上下文」的时候调（Windows 上是 legacy bootstrap 上下文）。
// 返回 false 只表示两件事之一：连 GL 1.1 那批入口都取不到，或者上下文自己报的版本低于
// 4.5 —— 后端建的就是 4.5 Core，低于这个版本说明入口表不完整。
bool load_procs();
GLADapiproc get_proc(const char* name);

#if defined(_WIN32)
using PFN_wglCreateContextAttribsARB = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using PFN_wglChoosePixelFormatARB = BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*,
                                                  UINT*);
extern PFN_wglCreateContextAttribsARB CreateContextAttribsARB;
extern PFN_wglChoosePixelFormatARB ChoosePixelFormatARB;
#endif

}  // namespace tamias::gl
