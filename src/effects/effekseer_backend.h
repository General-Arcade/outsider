/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_EFFEKSEER_BACKEND_H
#define RMMZ_EFFEKSEER_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to a loaded Effekseer effect resource. */
typedef uint32_t EffectHandle;
#define EFFECT_HANDLE_INVALID 0

/* Handle to a playing effect instance. */
typedef int32_t EffectInstanceHandle;
#define EFFECT_INSTANCE_INVALID (-1)

/* Initialize the backend (SDK, or a tracking stub when the SDK is not built).
   max_sprites bounds the renderer's instance count (8000 is typical). */
bool effekseer_init(int screen_width, int screen_height, int max_sprites);

/* Shut down and release all resources. Safe to call when uninitialized. */
void effekseer_shutdown(void);

/* Returns true if initialized (real or stub). */
bool effekseer_is_initialized(void);

/* Load an .efkefc effect with the given load-time scale.
   Returns EFFECT_HANDLE_INVALID on failure. */
EffectHandle effekseer_load(const char *path, float scale);

/* Release a loaded effect and free its resources. */
void effekseer_release(EffectHandle handle);

/* Check whether an effect has finished loading (always true for stub). */
bool effekseer_is_loaded(EffectHandle handle);

/* Play a loaded effect at position (x, y, z).
   Returns EFFECT_INSTANCE_INVALID on failure. */
EffectInstanceHandle effekseer_play(EffectHandle effect, float x, float y, float z);

/* Stop a specific playing instance. */
void effekseer_stop(EffectInstanceHandle instance);

/* Stop the root node of a playing instance (lets child particles finish). */
void effekseer_stop_root(EffectInstanceHandle instance);

/* Stop all currently playing effect instances. */
void effekseer_stop_all(void);

/* Set the 3D position of a playing instance. */
void effekseer_set_position(EffectInstanceHandle instance, float x, float y, float z);

/* Set the rotation (in radians) of a playing instance. */
void effekseer_set_rotation(EffectInstanceHandle instance, float x, float y, float z);

/* Set the scale of a playing instance. */
void effekseer_set_scale(EffectInstanceHandle instance, float x, float y, float z);

/* Set the playback speed of a playing instance (1.0 = normal). */
void effekseer_set_speed(EffectInstanceHandle instance, float speed);

/* Check whether an instance is still playing/exists. */
bool effekseer_exists(EffectInstanceHandle instance);

/* Set the projection matrix (column-major 4x4 = 16 floats). */
void effekseer_set_projection_matrix(const float *matrix);

/* Set the camera/view matrix (column-major 4x4 = 16 floats). */
void effekseer_set_camera_matrix(const float *matrix);

/* Update all playing effects by one frame (call once per game frame). */
void effekseer_update(float delta_frames);

/* Begin the effect drawing pass. Must be called before drawHandle(). */
void effekseer_begin_draw(void);

/* Draw a specific effect instance. */
void effekseer_draw_handle(EffectInstanceHandle instance);

/* End the effect drawing pass. */
void effekseer_end_draw(void);

/* Set whether Effekseer should save/restore OpenGL state. */
void effekseer_set_restoration_of_states_flag(bool flag);

/* Set a background texture for distortion effects (0 to clear). */
void effekseer_set_background(uint32_t gl_texture);

/* Reset background texture. */
void effekseer_reset_background(void);

/* Get the number of currently loaded effects (for testing). */
int effekseer_get_loaded_count(void);

/* Get the number of currently playing instances (for testing). */
int effekseer_get_playing_count(void);

#ifdef __cplusplus
}
#endif

#endif /* RMMZ_EFFEKSEER_BACKEND_H */
