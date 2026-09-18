/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_GPU_BACKEND_H
#define RMMZ_GPU_BACKEND_H

/*
 * Shared state for the SDL3 GPU implementations of renderer.h, sprite_batch.h
 * and filters.h. Not a public interface: those three headers stay unchanged so
 * the bindings, the JS shims and the tests do not know which backend is built.
 *
 * Two things shape this design.
 *
 * Deferred recording. SDL3 forbids a copy pass while a render pass is open, so
 * vertex data cannot be uploaded in the middle of drawing. Draws are therefore
 * recorded into a command list with their vertices in one CPU arena;
 * gpu_submit() uploads the arena once and replays the list. This is the same
 * shape as SDL's own GPU render backend.
 *
 * OpenGL orientation. The JS layer was written against the GL backend and
 * assumes a render target's first texture row is the *bottom* of the image
 * (see the flipped V coordinates in pixi_shim.js). Render targets here use a
 * Y-flipped projection to reproduce that, so nothing above the C layer
 * changes. The frame is drawn into an offscreen screen texture -- SDL cannot
 * read back a swapchain texture, and screenshots need to -- and blitted to the
 * swapchain, flipped, at the end of the frame.
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* Vertex layout shared with the sprite batch: matches the GL backend. */
typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
} GpuVertex;

/* Built-in shader programs. Ids are stable and are what filter_compile_shader
   hands back to JS, so 0 keeps meaning "no shader". */
enum {
    GPU_SHADER_NONE         = 0,
    GPU_SHADER_SPRITE       = 1,
    GPU_SHADER_COLOR_MATRIX = 2,
    GPU_SHADER_BLUR         = 3,
    GPU_SHADER_ALPHA        = 4,
    GPU_SHADER_COLOR_FILTER = 5,
    GPU_SHADER_MASK         = 6,
    GPU_SHADER_BLIT         = 7,
    GPU_SHADER_COUNT        = 8,
};

/* Largest fragment uniform block of any built-in shader (colour matrix). */
#define GPU_MAX_UNIFORM_BYTES 80

/* Device lifecycle */

/* Create the GPU device and claim the window. Returns false when no supported
   backend is available, in which case the caller should fall back. */
bool gpu_backend_init(SDL_Window *window);
void gpu_backend_shutdown(void);
bool gpu_backend_ready(void);
SDL_GPUDevice *gpu_backend_device(void);

/* Texture registry: the JS layer passes textures around as integer ids, as it
   did with GL names, so SDL_GPUTexture pointers live behind a handle table. */

uint32_t gpu_texture_create(int width, int height, bool render_target);
void     gpu_texture_destroy(uint32_t id);
void     gpu_texture_upload(uint32_t id, int width, int height, const uint8_t *pixels);
void     gpu_texture_set_filter(uint32_t id, bool linear);
SDL_GPUTexture *gpu_texture_handle(uint32_t id, int *w, int *h);

/* Read a texture back into a malloc'd width*height*4 buffer. Pass 0 for the
   screen texture. Rows come back in GL order (first row = bottom). */
uint8_t *gpu_texture_download(uint32_t id, int width, int height);

/* Frame and command recording */

/* Resize the offscreen screen texture the frame is drawn into. */
void gpu_frame_resize(int width, int height);
void gpu_frame_present(void);

/* Dimensions of that screen texture, which tracks the window. */
void gpu_screen_size(int *w, int *h);

/* Switch render target. id 0 selects the screen texture. */
void gpu_record_target(uint32_t id, int width, int height);
void gpu_record_clear(float r, float g, float b, float a);
void gpu_record_viewport(int x, int y, int w, int h);
/* Scissor rectangles arrive in GL's bottom-left origin and are flipped here. */
void gpu_record_scissor(int x, int y, int w, int h);
void gpu_record_scissor_off(void);

/* Append quads to the vertex arena and record a draw for them. */
void gpu_record_sprites(const GpuVertex *verts, int quad_count, uint32_t texture,
                        int blend_mode, const float projection[16],
                        float premultiplied);

/* Record a fullscreen filter pass. `uniforms` may be NULL. */
void gpu_record_filter(int shader, uint32_t texture, uint32_t mask_texture,
                       const void *uniforms, uint32_t uniform_size);

/* Upload the arenas, replay everything recorded so far and submit. Called at
   end of frame, and before any readback that must see prior draws. */
void gpu_submit(void);

#endif /* RMMZ_GPU_BACKEND_H */
