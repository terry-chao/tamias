#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#include <GL/glx.h>
#endif

// Minimal OpenGL 4.5 core loader used by the Tamias OpenGL RHI.
// Tokens / prototypes that may be missing from system GL headers.

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_DEPTH_TEST
#define GL_DEPTH_TEST 0x0B71
#endif
#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif
#ifndef GL_CULL_FACE
#define GL_CULL_FACE 0x0B44
#endif
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK 0x0408
#endif
#ifndef GL_LINE
#define GL_LINE 0x1B01
#endif
#ifndef GL_FILL
#define GL_FILL 0x1B02
#endif
#ifndef GL_FLOAT
#define GL_FLOAT 0x1406
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif
#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004
#endif
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif
#ifndef GL_DEPTH_BUFFER_BIT
#define GL_DEPTH_BUFFER_BIT 0x00000100
#endif
#ifndef GL_LESS
#define GL_LESS 0x0201
#endif
#ifndef GL_FALSE
#define GL_FALSE 0
#endif
#ifndef GL_TRUE
#define GL_TRUE 1
#endif
#ifndef GL_SHADER_BINARY_FORMAT_SPIR_V
#define GL_SHADER_BINARY_FORMAT_SPIR_V 0x9551
#endif
#ifndef GL_SPIR_V_BINARY
#define GL_SPIR_V_BINARY 0x9552
#endif

using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

namespace tamias::gl {

bool load_procs();
void* get_proc(const char* name);

extern void (*GenBuffers)(GLsizei n, GLuint* buffers);
extern void (*DeleteBuffers)(GLsizei n, const GLuint* buffers);
extern void (*BindBuffer)(GLenum target, GLuint buffer);
extern void (*BufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
extern void (*BufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
extern void (*BindBufferBase)(GLenum target, GLuint index, GLuint buffer);

extern void (*GenVertexArrays)(GLsizei n, GLuint* arrays);
extern void (*DeleteVertexArrays)(GLsizei n, const GLuint* arrays);
extern void (*BindVertexArray)(GLuint array);
extern void (*EnableVertexAttribArray)(GLuint index);
extern void (*VertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized,
                                   GLsizei stride, const void* pointer);

extern GLuint (*CreateShader)(GLenum type);
extern void (*DeleteShader)(GLuint shader);
extern void (*ShaderSource)(GLuint shader, GLsizei count, const GLchar* const* string,
                            const GLint* length);
extern void (*CompileShader)(GLuint shader);
extern void (*ShaderBinary)(GLsizei count, const GLuint* shaders, GLenum binaryFormat,
                            const void* binary, GLsizei length);
extern void (*SpecializeShader)(GLuint shader, const GLchar* pEntryPoint,
                                GLuint numSpecializationConstants, const GLuint* pConstantIndex,
                                const GLuint* pConstantValue);
extern void (*GetShaderiv)(GLuint shader, GLenum pname, GLint* params);
extern void (*GetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);

extern GLuint (*CreateProgram)();
extern void (*DeleteProgram)(GLuint program);
extern void (*AttachShader)(GLuint program, GLuint shader);
extern void (*LinkProgram)(GLuint program);
extern void (*UseProgram)(GLuint program);
extern void (*GetProgramiv)(GLuint program, GLenum pname, GLint* params);
extern void (*GetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);

extern void (*Enable)(GLenum cap);
extern void (*Disable)(GLenum cap);
extern void (*DepthFunc)(GLenum func);
extern void (*DepthMask)(GLboolean flag);
extern void (*Clear)(GLbitfield mask);
extern void (*ClearColor)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
extern void (*ClearDepth)(GLdouble depth);
extern void (*Viewport)(GLint x, GLint y, GLsizei width, GLsizei height);
extern void (*Scissor)(GLint x, GLint y, GLsizei width, GLsizei height);
extern void (*PolygonMode)(GLenum face, GLenum mode);
extern void (*DrawElements)(GLenum mode, GLsizei count, GLenum type, const void* indices);
extern void (*Finish)();
extern GLenum (*GetError)();
extern const GLubyte* (*GetString)(GLenum name);

#if defined(_WIN32)
using PFN_wglCreateContextAttribsARB = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using PFN_wglChoosePixelFormatARB = BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*,
                                                  UINT*);
extern PFN_wglCreateContextAttribsARB CreateContextAttribsARB;
extern PFN_wglChoosePixelFormatARB ChoosePixelFormatARB;
#endif

}  // namespace tamias::gl
