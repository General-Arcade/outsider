/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_FILE_IO_H
#define RMMZ_FILE_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Set the directory that relative game paths resolve against.
   NULL or "" means the current working directory. */
void file_io_set_game_root(const char *root);

/* Get the current game root directory ("" if not set). */
const char *file_io_get_game_root(void);

/* Resolve a game path to a filesystem path inside the game root. Returns
   false on overflow or if the path escapes the root (traversal, outside prefix). */
bool file_io_resolve_path(const char *relative_path, char *out, size_t out_size);

/* Read a whole file as a NUL-terminated string (free() it). Returns NULL on
   error; out_size, if non-NULL, receives the byte count. */
char *file_io_read_text(const char *path, size_t *out_size);

/* Read a whole file as bytes (free() it). Returns NULL on error. */
uint8_t *file_io_read_binary(const char *path, size_t *out_size);

/* Write a NUL-terminated string to a file. Returns true on success. */
bool file_io_write_text(const char *path, const char *data);

/* Write binary data to a file. Returns true on success. */
bool file_io_write_binary(const char *path, const uint8_t *data, size_t size);

/* Check if a path exists (file or directory). */
bool file_io_exists(const char *path);

/* Check if a path is a directory. */
bool file_io_is_directory(const char *path);

/* Create a directory (and parents if needed). Returns true on success. */
bool file_io_mkdir(const char *path);

/* List directory entries (excluding . and ..) as a NULL-terminated array.
   Returns NULL on error; free with file_io_free_dirlist(). */
char **file_io_readdir(const char *path, size_t *out_count);

/* Free a directory listing returned by file_io_readdir(). */
void file_io_free_dirlist(char **list);

/* Delete a file. Returns true on success. */
bool file_io_unlink(const char *path);

/* Rename/move a file. Returns true on success. */
bool file_io_rename(const char *old_path, const char *new_path);

#endif /* RMMZ_FILE_IO_H */
