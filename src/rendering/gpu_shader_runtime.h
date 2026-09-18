/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_GPU_SHADER_RUNTIME_H
#define RMMZ_GPU_SHADER_RUNTIME_H

#include <SDL3/SDL.h>
#include <stdbool.h>

/*
 * Compiles shaders at run time, for the filters a game's plugins bring with
 * them. SDL3's GPU API takes bytecode, so the chain is:
 *
 *   plugin GLSL ES  --glsl_translate-->  Vulkan GLSL
 *                   --glslang-------->   SPIR-V
 *                   --SDL_shadercross->  the device's own format
 *
 * shadercross reaches D3D12 through DXBC, which Windows' own d3dcompiler
 * produces, so nothing here needs a redistributable compiler alongside it.
 *
 * Built without RMMZ_RUNTIME_SHADERS these become stubs that report
 * unavailability, and the caller falls back to drawing unfiltered.
 */

/* Start up the compiler. Safe to call more than once. */
bool gpu_shader_runtime_init(void);
void gpu_shader_runtime_shutdown(void);

/* False when the runtime was built without shader compilation. */
bool gpu_shader_runtime_available(void);

/* Compile Vulkan GLSL 450 into a shader for `device`. `sampler_count` and
   `uniform_buffer_count` describe what the source declares. Returns NULL on
   failure, having logged the compiler's diagnostic.

   Both stages of a pipeline must come through here together: on D3D12 the
   compiler emits DXBC, which cannot share a pipeline with the DXIL the
   built-in shaders ship as. */
SDL_GPUShader *gpu_shader_runtime_compile(SDL_GPUDevice *device,
                                          const char *glsl450,
                                          bool vertex_stage,
                                          int sampler_count,
                                          int uniform_buffer_count);

/* The fullscreen vertex stage a runtime filter pairs with, in the same
   bytecode format. Owned by the module; do not release. */
SDL_GPUShader *gpu_shader_runtime_fullscreen_vertex(SDL_GPUDevice *device);

#endif /* RMMZ_GPU_SHADER_RUNTIME_H */
