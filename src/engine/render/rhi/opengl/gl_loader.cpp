#include "gl_loader.h"

namespace tamias::gl {

#if defined(_WIN32)
PFN_wglCreateContextAttribsARB CreateContextAttribsARB = nullptr;
PFN_wglChoosePixelFormatARB ChoosePixelFormatARB = nullptr;
#endif

namespace {

// GL 入口点整表交给 gladLoadGL；这个模板只服务 WGL 那两个扩展入口。
template <typename T>
bool load(T& fn, const char* name) {
  fn = reinterpret_cast<T>(get_proc(name));
  return fn != nullptr;
}

}  // namespace

GLADapiproc get_proc(const char* name) {
#if defined(_WIN32)
  // wglGetProcAddress 不只返回 NULL 表示失败：驱动还会用 1 / 2 / 3 / -1 这几个哨兵值
  // （WGL 规范），照原样强转就会调用到地址 1。遇到哨兵要退回 opengl32.dll 里找 ——
  // GL 1.1 的入口本来就只在那里面。
  GLADapiproc p = reinterpret_cast<GLADapiproc>(wglGetProcAddress(name));
  if (p == nullptr || p == reinterpret_cast<GLADapiproc>(1) ||
      p == reinterpret_cast<GLADapiproc>(2) || p == reinterpret_cast<GLADapiproc>(3) ||
      p == reinterpret_cast<GLADapiproc>(-1)) {
    static HMODULE module = GetModuleHandleA("opengl32.dll");
    if (module) {
      p = reinterpret_cast<GLADapiproc>(GetProcAddress(module, name));
    }
  }
  return p;
#else
  return reinterpret_cast<GLADapiproc>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
#endif
}

bool load_procs() {
  // gladLoadGL 会对每个名字调 get_proc，把指针和 GLAD_GL_* 标志都填上（取不到的留 NULL）。
  // 它自己只回报「解析出来的 GL 版本」，所以这里再要求上下文报的是 4.5 以上：
  // 后端建的就是 4.5 Core，低于这个版本就意味着入口表不完整，早点失败比后面空指针好。
  const bool gl_ok = gladLoadGL(get_proc) != 0 && GLAD_GL_VERSION_4_5 != 0;
#if defined(_WIN32)
  // 建 4.5 Core 上下文要用这两个 WGL 扩展入口；ChoosePixelFormatARB 允许缺席
  // （调用点会先判空再退回 legacy 的 ChoosePixelFormat）。
  const bool wgl_ok = load(CreateContextAttribsARB, "wglCreateContextAttribsARB");
  load(ChoosePixelFormatARB, "wglChoosePixelFormatARB");
  return gl_ok && wgl_ok;
#else
  return gl_ok;
#endif
}

}  // namespace tamias::gl
