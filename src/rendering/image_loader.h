/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_IMAGE_LOADER_H
#define RMMZ_IMAGE_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Opaque handle representing a loaded image in the cache. */
typedef uint32_t ImageHandle;

#define IMAGE_HANDLE_INVALID 0

/* Image metadata returned by queries. */
typedef struct {
    int      width;
    int      height;
    uint32_t gl_texture;  /* OpenGL texture ID (0 if no GL context) */
} ImageInfo;

/* Initialize the image loader subsystem.
   If `enable_gl` is true, decoded images are uploaded as OpenGL textures.
   Pass false for headless / test environments. */
void image_loader_init(bool enable_gl);

/* Shut down and free all cached images and textures. */
void image_loader_shutdown(void);

/* Load an image from a file path. Returns a cached handle.
   If the image was previously loaded, returns the cached handle.
   Returns IMAGE_HANDLE_INVALID on failure. */
ImageHandle image_load(const char *path);

/* Load an image from an in-memory buffer (e.g. from a Blob).
   The `key` is used for cache lookup (e.g. a blob: URL).
   Returns IMAGE_HANDLE_INVALID on failure. */
ImageHandle image_load_from_memory(const char *key,
                                   const uint8_t *data, size_t size);

/* Get info about a loaded image. Returns false if handle is invalid. */
bool image_get_info(ImageHandle handle, ImageInfo *out);

/* Get raw RGBA pixel data for a loaded image.
   Returns NULL if handle is invalid. Caller must NOT free the returned pointer. */
const uint8_t *image_get_pixels(ImageHandle handle);

/* Release a cached image (decrements ref or removes from cache).
   After this call the handle may become invalid. */
void image_free(ImageHandle handle);

/* Return the number of currently cached images. */
size_t image_cache_count(void);

/* Evict all entries from the cache and free resources. */
void image_cache_clear(void);

/* Upload raw RGBA pixels as a standalone GL texture (nearest filtering,
   clamped). Not cached; the caller owns the returned id and deletes it via
   the renderer. Returns 0 when GL is disabled. */
uint32_t image_upload_rgba_texture(const uint8_t *rgba, int width, int height);

#endif /* RMMZ_IMAGE_LOADER_H */
