/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_GL_LOADER_H
#define RMMZ_GL_LOADER_H

/*
 * OpenGL function loader. The renderer targets OpenGL 4.5 core, but on Windows
 * opengl32.dll only exports GL 1.1, so every newer entry point is resolved at
 * runtime via SDL_GL_GetProcAddress and mapped onto its standard gl* name.
 * gl_loader_init() must be called once after SDL_GL_CreateContext().
 */

#include <SDL_opengl.h>

/* X-macro list of every post-GL-1.1 function used by the runtime. */
#define RMMZ_GL_FUNCS(X) \
    /* Shaders / programs (GL 2.0) */ \
    X(PFNGLCREATESHADERPROC,               glCreateShader) \
    X(PFNGLSHADERSOURCEPROC,               glShaderSource) \
    X(PFNGLCOMPILESHADERPROC,              glCompileShader) \
    X(PFNGLGETSHADERIVPROC,                glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC,           glGetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC,               glDeleteShader) \
    X(PFNGLCREATEPROGRAMPROC,              glCreateProgram) \
    X(PFNGLATTACHSHADERPROC,               glAttachShader) \
    X(PFNGLLINKPROGRAMPROC,                glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC,               glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC,          glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC,                 glUseProgram) \
    X(PFNGLDELETEPROGRAMPROC,              glDeleteProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC,         glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC,                  glUniform1i) \
    X(PFNGLUNIFORM1FPROC,                  glUniform1f) \
    X(PFNGLUNIFORM2FPROC,                  glUniform2f) \
    X(PFNGLUNIFORM4FPROC,                  glUniform4f) \
    X(PFNGLUNIFORMMATRIX4FVPROC,           glUniformMatrix4fv) \
    /* Textures / blending (GL 1.3 - 1.4) */ \
    X(PFNGLACTIVETEXTUREPROC,              glActiveTexture) \
    X(PFNGLBLENDFUNCSEPARATEPROC,          glBlendFuncSeparate) \
    /* Buffers / vertex arrays / framebuffers (GL 1.5 - 3.0) */ \
    X(PFNGLDELETEBUFFERSPROC,              glDeleteBuffers) \
    X(PFNGLBINDVERTEXARRAYPROC,            glBindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC,         glDeleteVertexArrays) \
    X(PFNGLBINDFRAMEBUFFERPROC,            glBindFramebuffer) \
    X(PFNGLDELETEFRAMEBUFFERSPROC,         glDeleteFramebuffers) \
    /* Direct state access (GL 4.5) */ \
    X(PFNGLCREATEBUFFERSPROC,              glCreateBuffers) \
    X(PFNGLNAMEDBUFFERSTORAGEPROC,         glNamedBufferStorage) \
    X(PFNGLNAMEDBUFFERDATAPROC,            glNamedBufferData) \
    X(PFNGLNAMEDBUFFERSUBDATAPROC,         glNamedBufferSubData) \
    X(PFNGLCREATEVERTEXARRAYSPROC,         glCreateVertexArrays) \
    X(PFNGLENABLEVERTEXARRAYATTRIBPROC,    glEnableVertexArrayAttrib) \
    X(PFNGLVERTEXARRAYATTRIBFORMATPROC,    glVertexArrayAttribFormat) \
    X(PFNGLVERTEXARRAYATTRIBBINDINGPROC,   glVertexArrayAttribBinding) \
    X(PFNGLVERTEXARRAYVERTEXBUFFERPROC,    glVertexArrayVertexBuffer) \
    X(PFNGLVERTEXARRAYELEMENTBUFFERPROC,   glVertexArrayElementBuffer) \
    X(PFNGLCREATETEXTURESPROC,             glCreateTextures) \
    X(PFNGLTEXTURESTORAGE2DPROC,           glTextureStorage2D) \
    X(PFNGLTEXTURESUBIMAGE2DPROC,          glTextureSubImage2D) \
    X(PFNGLTEXTUREPARAMETERIPROC,          glTextureParameteri) \
    X(PFNGLCREATEFRAMEBUFFERSPROC,         glCreateFramebuffers) \
    X(PFNGLNAMEDFRAMEBUFFERTEXTUREPROC,    glNamedFramebufferTexture) \
    X(PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC, glCheckNamedFramebufferStatus) \
    X(PFNGLCLEARNAMEDFRAMEBUFFERFVPROC,    glClearNamedFramebufferfv)

/* Declare one function pointer per entry point (rmmz_glFoo). */
#define RMMZ_GL_DECLARE(type, name) extern type rmmz_##name;
RMMZ_GL_FUNCS(RMMZ_GL_DECLARE)
#undef RMMZ_GL_DECLARE

/* Route the standard names through the loaded pointers. */
#define glCreateShader                 rmmz_glCreateShader
#define glShaderSource                 rmmz_glShaderSource
#define glCompileShader                rmmz_glCompileShader
#define glGetShaderiv                  rmmz_glGetShaderiv
#define glGetShaderInfoLog             rmmz_glGetShaderInfoLog
#define glDeleteShader                 rmmz_glDeleteShader
#define glCreateProgram                rmmz_glCreateProgram
#define glAttachShader                 rmmz_glAttachShader
#define glLinkProgram                  rmmz_glLinkProgram
#define glGetProgramiv                 rmmz_glGetProgramiv
#define glGetProgramInfoLog            rmmz_glGetProgramInfoLog
#define glUseProgram                   rmmz_glUseProgram
#define glDeleteProgram                rmmz_glDeleteProgram
#define glGetUniformLocation           rmmz_glGetUniformLocation
#define glUniform1i                    rmmz_glUniform1i
#define glUniform1f                    rmmz_glUniform1f
#define glUniform2f                    rmmz_glUniform2f
#define glUniform4f                    rmmz_glUniform4f
#define glUniformMatrix4fv             rmmz_glUniformMatrix4fv
#define glActiveTexture                rmmz_glActiveTexture
#define glBlendFuncSeparate            rmmz_glBlendFuncSeparate
#define glDeleteBuffers                rmmz_glDeleteBuffers
#define glBindVertexArray              rmmz_glBindVertexArray
#define glDeleteVertexArrays           rmmz_glDeleteVertexArrays
#define glBindFramebuffer              rmmz_glBindFramebuffer
#define glDeleteFramebuffers           rmmz_glDeleteFramebuffers
#define glCreateBuffers                rmmz_glCreateBuffers
#define glNamedBufferStorage           rmmz_glNamedBufferStorage
#define glNamedBufferData              rmmz_glNamedBufferData
#define glNamedBufferSubData           rmmz_glNamedBufferSubData
#define glCreateVertexArrays           rmmz_glCreateVertexArrays
#define glEnableVertexArrayAttrib      rmmz_glEnableVertexArrayAttrib
#define glVertexArrayAttribFormat      rmmz_glVertexArrayAttribFormat
#define glVertexArrayAttribBinding     rmmz_glVertexArrayAttribBinding
#define glVertexArrayVertexBuffer      rmmz_glVertexArrayVertexBuffer
#define glVertexArrayElementBuffer     rmmz_glVertexArrayElementBuffer
#define glCreateTextures               rmmz_glCreateTextures
#define glTextureStorage2D             rmmz_glTextureStorage2D
#define glTextureSubImage2D            rmmz_glTextureSubImage2D
#define glTextureParameteri            rmmz_glTextureParameteri
#define glCreateFramebuffers           rmmz_glCreateFramebuffers
#define glNamedFramebufferTexture      rmmz_glNamedFramebufferTexture
#define glCheckNamedFramebufferStatus  rmmz_glCheckNamedFramebufferStatus
#define glClearNamedFramebufferfv      rmmz_glClearNamedFramebufferfv

/* Resolve every entry point through SDL_GL_GetProcAddress.
   Requires a current OpenGL context. Returns 0 on success, or the number of
   functions that could not be resolved (each is logged to stderr). */
int gl_loader_init(void);

/* Non-zero once gl_loader_init() has succeeded. */
int gl_loader_is_ready(void);

#endif /* RMMZ_GL_LOADER_H */
