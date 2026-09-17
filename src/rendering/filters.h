/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_FILTERS_H
#define RMMZ_FILTERS_H

#include <stdint.h>

/* Native filter rendering system for PIXI.js filter compatibility.
   Provides fullscreen quad rendering with custom shaders for
   post-processing effects (color matrix, blur, alpha). */

/* Initialize the filter system. Creates shared fullscreen quad mesh.
   Must be called after OpenGL context is available. */
void filters_init(void);

/* Shut down and free all filter resources. */
void filters_shutdown(void);

/* Compile a filter shader program from vertex and fragment sources.
   If vert_src is NULL, uses the default passthrough vertex shader.
   Returns program ID (0 on failure or when GL is unavailable). */
uint32_t filter_compile_shader(const char *vert_src, const char *frag_src);

/* Delete a compiled filter shader. */
void filter_delete_shader(uint32_t program);

/* Begin filter rendering: bind shader, set input texture, resolution.
   Must call filter_end() when done. */
void filter_begin(uint32_t program, uint32_t input_texture, int width, int height);

/* Set a float uniform on the currently bound filter shader. */
void filter_set_uniform_1f(uint32_t program, const char *name, float value);

/* Set a vec2 uniform. */
void filter_set_uniform_2f(uint32_t program, const char *name, float x, float y);

/* Set a vec4 uniform. */
void filter_set_uniform_4f(uint32_t program, const char *name,
                           float x, float y, float z, float w);

/* Set a mat4 uniform (16 floats, column-major). */
void filter_set_uniform_mat4(uint32_t program, const char *name, const float *values);

/* Draw the fullscreen quad with the current shader. */
void filter_draw_quad(void);

/* End filter rendering: unbinds shader and VAO. */
void filter_end(void);

/* Built-in shader source getters. */
const char *filter_default_vert_src(void);
const char *filter_color_matrix_frag_src(void);
const char *filter_blur_frag_src(void);
const char *filter_alpha_frag_src(void);
const char *filter_color_filter_frag_src(void);

#endif /* RMMZ_FILTERS_H */
