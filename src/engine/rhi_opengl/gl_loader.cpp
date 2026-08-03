#include "gl_loader.h"

#if !defined(_WIN32)
#include <GL/glx.h>
#endif

namespace tamias::gl {
namespace {

template <typename T>
bool load(T& fn, const char* name) {
  fn = reinterpret_cast<T>(get_proc(name));
  return fn != nullptr;
}

}  // namespace

void (*GenBuffers)(GLsizei, GLuint*) = nullptr;
void (*DeleteBuffers)(GLsizei, const GLuint*) = nullptr;
void (*BindBuffer)(GLenum, GLuint) = nullptr;
void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
void (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
void (*BindBufferBase)(GLenum, GLuint, GLuint) = nullptr;

void (*GenVertexArrays)(GLsizei, GLuint*) = nullptr;
void (*DeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
void (*BindVertexArray)(GLuint) = nullptr;
void (*EnableVertexAttribArray)(GLuint) = nullptr;
void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;

GLuint (*CreateShader)(GLenum) = nullptr;
void (*DeleteShader)(GLuint) = nullptr;
void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
void (*CompileShader)(GLuint) = nullptr;
void (*ShaderBinary)(GLsizei, const GLuint*, GLenum, const void*, GLsizei) = nullptr;
void (*SpecializeShader)(GLuint, const GLchar*, GLuint, const GLuint*, const GLuint*) = nullptr;
void (*GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;

GLuint (*CreateProgram)() = nullptr;
void (*DeleteProgram)(GLuint) = nullptr;
void (*AttachShader)(GLuint, GLuint) = nullptr;
void (*LinkProgram)(GLuint) = nullptr;
void (*UseProgram)(GLuint) = nullptr;
void (*GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;

void (*Enable)(GLenum) = nullptr;
void (*Disable)(GLenum) = nullptr;
void (*DepthFunc)(GLenum) = nullptr;
void (*DepthMask)(GLboolean) = nullptr;
void (*Clear)(GLbitfield) = nullptr;
void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
void (*ClearDepth)(GLdouble) = nullptr;
void (*Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
void (*Scissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
void (*PolygonMode)(GLenum, GLenum) = nullptr;
void (*DrawElements)(GLenum, GLsizei, GLenum, const void*) = nullptr;
void (*Finish)() = nullptr;
GLenum (*GetError)() = nullptr;
const GLubyte* (*GetString)(GLenum) = nullptr;

#if defined(_WIN32)
PFN_wglCreateContextAttribsARB CreateContextAttribsARB = nullptr;
PFN_wglChoosePixelFormatARB ChoosePixelFormatARB = nullptr;
#endif

void* get_proc(const char* name) {
#if defined(_WIN32)
  void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
  if (p == nullptr || p == reinterpret_cast<void*>(1) || p == reinterpret_cast<void*>(2) ||
      p == reinterpret_cast<void*>(3) || p == reinterpret_cast<void*>(-1)) {
    static HMODULE module = GetModuleHandleA("opengl32.dll");
    if (module) {
      p = reinterpret_cast<void*>(GetProcAddress(module, name));
    }
  }
  return p;
#else
  return reinterpret_cast<void*>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
#endif
}

bool load_procs() {
  bool ok = true;
  ok = load(GenBuffers, "glGenBuffers") && ok;
  ok = load(DeleteBuffers, "glDeleteBuffers") && ok;
  ok = load(BindBuffer, "glBindBuffer") && ok;
  ok = load(BufferData, "glBufferData") && ok;
  ok = load(BufferSubData, "glBufferSubData") && ok;
  ok = load(BindBufferBase, "glBindBufferBase") && ok;
  ok = load(GenVertexArrays, "glGenVertexArrays") && ok;
  ok = load(DeleteVertexArrays, "glDeleteVertexArrays") && ok;
  ok = load(BindVertexArray, "glBindVertexArray") && ok;
  ok = load(EnableVertexAttribArray, "glEnableVertexAttribArray") && ok;
  ok = load(VertexAttribPointer, "glVertexAttribPointer") && ok;
  ok = load(CreateShader, "glCreateShader") && ok;
  ok = load(DeleteShader, "glDeleteShader") && ok;
  ok = load(ShaderSource, "glShaderSource") && ok;
  ok = load(CompileShader, "glCompileShader") && ok;
  ok = load(ShaderBinary, "glShaderBinary") && ok;
  // Core in 4.6; on 4.5 + GL_ARB_gl_spirv the entry is glSpecializeShaderARB.
  if (!load(SpecializeShader, "glSpecializeShader")) {
    ok = load(SpecializeShader, "glSpecializeShaderARB") && ok;
  }
  ok = load(GetShaderiv, "glGetShaderiv") && ok;
  ok = load(GetShaderInfoLog, "glGetShaderInfoLog") && ok;
  ok = load(CreateProgram, "glCreateProgram") && ok;
  ok = load(DeleteProgram, "glDeleteProgram") && ok;
  ok = load(AttachShader, "glAttachShader") && ok;
  ok = load(LinkProgram, "glLinkProgram") && ok;
  ok = load(UseProgram, "glUseProgram") && ok;
  ok = load(GetProgramiv, "glGetProgramiv") && ok;
  ok = load(GetProgramInfoLog, "glGetProgramInfoLog") && ok;
  ok = load(Enable, "glEnable") && ok;
  ok = load(Disable, "glDisable") && ok;
  ok = load(DepthFunc, "glDepthFunc") && ok;
  ok = load(DepthMask, "glDepthMask") && ok;
  ok = load(Clear, "glClear") && ok;
  ok = load(ClearColor, "glClearColor") && ok;
  ok = load(ClearDepth, "glClearDepth") && ok;
  ok = load(Viewport, "glViewport") && ok;
  ok = load(Scissor, "glScissor") && ok;
  ok = load(PolygonMode, "glPolygonMode") && ok;
  ok = load(DrawElements, "glDrawElements") && ok;
  ok = load(Finish, "glFinish") && ok;
  ok = load(GetError, "glGetError") && ok;
  ok = load(GetString, "glGetString") && ok;
#if defined(_WIN32)
  CreateContextAttribsARB =
      reinterpret_cast<PFN_wglCreateContextAttribsARB>(get_proc("wglCreateContextAttribsARB"));
  ChoosePixelFormatARB =
      reinterpret_cast<PFN_wglChoosePixelFormatARB>(get_proc("wglChoosePixelFormatARB"));
  ok = CreateContextAttribsARB != nullptr && ok;
#endif
  return ok;
}

}  // namespace tamias::gl
