/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_SCRIPT_LOADER_H
#define RMMZ_SCRIPT_LOADER_H

#include "engine/js_engine.h"
#include <stdbool.h>
#include <stddef.h>

/* One entry parsed from plugins.js. */
typedef struct {
    char *name;     /* plugin filename without extension */
    bool  status;   /* enabled? */
} PluginEntry;

typedef struct {
    PluginEntry *entries;
    size_t       count;
} PluginList;

/* Parse plugins.js (var $plugins = [{name, status, ...}, ...]) into a list.
   Returns NULL on error; free with script_loader_free_plugins(). */
PluginList *script_loader_parse_plugins(const char *plugins_js_path);

/* Free a plugin list returned by script_loader_parse_plugins(). */
void script_loader_free_plugins(PluginList *list);

/* Load everything in order: shims, libs, rmmz core scripts, enabled plugins,
   post-core shims, main.js; then start the game. Returns false on fatal error. */
bool script_loader_load_all(JSEngine *engine, const char *shim_dir, const char *game_dir);

/* Load only the shim files (for tests without game files). */
bool script_loader_load_shims(JSEngine *engine, const char *shim_dir);

/* Return the script paths load_all would load, without loading them.
   Free with script_loader_free_path_list(). */
char **script_loader_get_load_order(const char *shim_dir, const char *game_dir, size_t *out_count);

/* Free a path list returned by script_loader_get_load_order(). */
void script_loader_free_path_list(char **list, size_t count);

#endif /* RMMZ_SCRIPT_LOADER_H */
