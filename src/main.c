/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "platform/platform.h"
#include "engine/js_engine.h"
#include "engine/game_loop.h"
#include "engine/script_loader.h"
#include "bindings/bind_io.h"
#include "bindings/bind_image.h"
#include "bindings/bind_canvas2d.h"
#include "bindings/bind_renderer.h"
#include "bindings/bind_font.h"
#include "bindings/bind_tilemap.h"
#include "bindings/bind_audio.h"
#include "bindings/bind_input.h"
#include "bindings/bind_effekseer.h"
#include "bindings/bind_video.h"
#include "bindings/bind_filters.h"
#include "bindings/bind_platform.h"
#include "engine/error_handler.h"
#include "audio/audio_engine.h"
#include "effects/effekseer_backend.h"
#include "video/video_player.h"
#include "input/input_manager.h"
#include "rendering/image_loader.h"
#include "rendering/canvas2d.h"
#include "rendering/font_manager.h"
#include "rendering/renderer.h"
#include "rendering/filters.h"
#include "io/file_io.h"
#include "platform/win_console.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifndef RMMZ_DEFAULT_WIDTH
#define RMMZ_DEFAULT_WIDTH 816
#endif
#ifndef RMMZ_DEFAULT_HEIGHT
#define RMMZ_DEFAULT_HEIGHT 624
#endif

#define APP_TITLE RMMZ_APP_NAME

/* Report a fatal startup error: log it and, where the platform provides one,
   show a dialog so the message is visible even without a console. */
static void fatal_error(const char *fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    error_handler_log(LOG_ERROR, "%s", msg);
    platform_show_error_dialog(APP_TITLE, msg);
}

/* If a directory exists at "<exe dir>/<name>", copy its path into out.
   Used to locate a packaged game/shims folder next to the executable so
   the runtime can be launched without arguments (e.g. by double-click). */
static bool find_dir_next_to_exe(const char *name, char *out, size_t out_size)
{
    char *base = SDL_GetBasePath();
    if (!base) return false;
    int n = snprintf(out, out_size, "%s%s", base, name);
    SDL_free(base);
    if (n <= 0 || (size_t)n >= out_size) return false;
    return file_io_is_directory(out);
}

int main(int argc, char *argv[])
{
    /* Parse command-line arguments. */
    const char *game_dir = NULL;
    const char *shim_dir = "src/shims";
    int win_w = RMMZ_DEFAULT_WIDTH;
    int win_h = RMMZ_DEFAULT_HEIGHT;
    bool explicit_resolution = false;
    int no_audio = 0;
    int max_frames = 0;
    const char *screenshot_path = NULL;
    const char *probe_expr = NULL;
    int watchdog_ms = 0;
    int perf_every = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            game_dir = argv[++i];
        } else if (strcmp(argv[i], "--shims") == 0 && i + 1 < argc) {
            shim_dir = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            /* Debug: exit cleanly after N frames. */
            max_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            /* Debug: write the last frame (see --frames) as a PPM file. */
            screenshot_path = argv[++i];
        } else if (strcmp(argv[i], "--probe") == 0 && i + 1 < argc) {
            /* Debug: JS expression logged when --frames stops the loop. */
            probe_expr = argv[++i];
        } else if (strcmp(argv[i], "--perf") == 0 && i + 1 < argc) {
            /* Debug: log average per-phase frame times every N frames. */
            perf_every = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--watchdog") == 0 && i + 1 < argc) {
            /* Debug: interrupt JS that blocks a frame or eval for longer than
               this many milliseconds, logging where it was. */
            watchdog_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &win_w, &win_h) != 2) {
                fatal_error("Invalid resolution format, expected WIDTHxHEIGHT");
                return 1;
            }
            explicit_resolution = true;
        } else if (strcmp(argv[i], "--no-audio") == 0) {
            no_audio = 1;
        } else if (argv[i][0] != '-') {
            /* Positional argument: treat as game directory. */
            game_dir = argv[i];
        }
    }

    /* No explicit paths: look for a packaged layout next to the executable
       (build_game.py produces <exe dir>/game/ with shims inside it). */
    static char default_game_dir[1024];
    static char default_shim_dir[1024];
    if (!game_dir && find_dir_next_to_exe("game", default_game_dir, sizeof(default_game_dir))) {
        game_dir = default_game_dir;
    }
    if (strcmp(shim_dir, "src/shims") == 0 && !file_io_is_directory(shim_dir)) {
        if (game_dir) {
            snprintf(default_shim_dir, sizeof(default_shim_dir), "%s/shims", game_dir);
            if (file_io_is_directory(default_shim_dir)) shim_dir = default_shim_dir;
        }
        if (shim_dir != default_shim_dir &&
            find_dir_next_to_exe("shims", default_shim_dir, sizeof(default_shim_dir))) {
            shim_dir = default_shim_dir;
        }
    }

    /* Determine log file paths — if a game directory is known, put them there. */
    const char *log_path = "debug.log";
    const char *console_log_path = "console.log";
    char log_path_buf[1024];
    char console_log_buf[1024];
    if (game_dir) {
        snprintf(log_path_buf, sizeof(log_path_buf), "%s/debug.log", game_dir);
        log_path = log_path_buf;
        snprintf(console_log_buf, sizeof(console_log_buf), "%s/console.log", game_dir);
        console_log_path = console_log_buf;
    }

    /* GUI-subsystem builds start without stdout/stderr; attach to the parent
       console if there is one, otherwise capture raw output to console.log. */
    platform_setup_console(console_log_path);
    /* Progress messages are few and matter most when the process dies before a
       normal exit would flush them, so do not buffer stdout. */
    setvbuf(stdout, NULL, _IONBF, 0);

    error_handler_init(log_path, LOG_INFO);
    error_handler_log(LOG_INFO, "Outsider (RPG Maker MZ native runtime) starting");

    /* Validate the game directory before opening a window, so a bad launch
       (wrong path, bare double-click) produces a clear error instead of a
       black screen. */
    if (!game_dir) {
        fatal_error("No game directory specified.\n\n"
                    "Run with:  outsider --game <path to RPG Maker MZ game>\n"
                    "or place a \"game\" folder next to the executable.");
        error_handler_shutdown();
        return 1;
    }
    {
        char main_js[1024];
        snprintf(main_js, sizeof(main_js), "%s/js/main.js", game_dir);
        if (!file_io_exists(main_js)) {
            fatal_error("Not an RPG Maker MZ game directory (js/main.js not found):\n%s",
                        game_dir);
            error_handler_shutdown();
            return 1;
        }
    }

    Platform *platform = platform_init(APP_TITLE, win_w, win_h);
    if (!platform) {
        fatal_error("Failed to initialize window or OpenGL 4.5 context.\n"
                    "Check that your graphics driver is up to date.");
        error_handler_shutdown();
        return 1;
    }

    /* The window opens in raw pixels (DPI-aware), while NW.js scales the
       game with the desktop. Match that so the default window looks the
       same size as the original player's; --resolution is taken literally. */
    if (!explicit_resolution) {
        float desktop_scale = platform_get_desktop_scale(platform);
        if (desktop_scale > 1.0f) {
            win_w = (int)(win_w * desktop_scale + 0.5f);
            win_h = (int)(win_h * desktop_scale + 0.5f);
            platform_set_window_size(platform, win_w, win_h);
            error_handler_log(LOG_INFO, "Desktop scale %.2f: window sized to %dx%d",
                              desktop_scale, win_w, win_h);
        }
    }

    error_handler_log(LOG_INFO, "Window created (%dx%d). OpenGL ready.",
                      win_w, win_h);

    JSEngine *js = js_engine_init();
    if (!js) {
        fatal_error("Failed to initialize JS engine");
        platform_shutdown(platform);
        error_handler_shutdown();
        return 1;
    }
    error_handler_log(LOG_INFO, "QuickJS engine initialized");
    if (watchdog_ms > 0) {
        js_engine_set_watchdog(js, (unsigned)watchdog_ms);
        error_handler_log(LOG_INFO, "Script watchdog enabled: %d ms", watchdog_ms);
    }

    bind_io_register(js_engine_get_context(js));

    /* Initialize image loader (GL enabled since we have a context). */
    image_loader_init(true);
    bind_image_register(js_engine_get_context(js));

    canvas2d_init();
    bind_canvas2d_register(js_engine_get_context(js));

    font_manager_init();
    bind_font_register(js_engine_get_context(js));

    bind_renderer_register(js_engine_get_context(js));

    bind_filters_register(js_engine_get_context(js));

    /* Register tilemap bindings (depends on renderer). */
    bind_tilemap_register(js_engine_get_context(js));

    if (no_audio) {
        audio_engine_set_disabled(true);
    } else {
        audio_engine_init();
    }
    bind_audio_register(js_engine_get_context(js));

    input_manager_init();
    bind_input_register(js_engine_get_context(js));

    bind_platform_register(js_engine_get_context(js), platform);

    effekseer_init(win_w, win_h, 8000);
    bind_effekseer_register(js_engine_get_context(js));

    {
        extern Renderer *s_renderer;
        video_player_init(&s_renderer);
    }
    bind_video_register(js_engine_get_context(js));

    file_io_set_game_root(game_dir);

    int scripts_ok = 1;
    error_handler_log(LOG_INFO, "Loading game from: %s", game_dir);
    if (!script_loader_load_all(js, shim_dir, game_dir)) {
        /* A bad --shims/--game path must not produce a black window that
           exits 0. Fail loudly instead. */
        fatal_error("Failed to load game scripts from:\n%s\n\nShims: %s\n\n"
                    "Check the --game and --shims paths (see debug.log).",
                    game_dir, shim_dir);
        scripts_ok = 0;
    }

    /* Initialize display info in JS (screen size, devicePixelRatio). */
    js_engine_eval(js, "typeof __dom_initDisplayInfo === 'function' && __dom_initDisplayInfo();",
                   "<display-init>");

    /* Propagate actual window size to JS so letterbox scaling and mouse
       coordinate conversion are correct from the first frame. */
    {
        int init_w, init_h;
        platform_get_window_size(platform, &init_w, &init_h);
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "__dom_setWindowSize && __dom_setWindowSize(%d, %d);",
                 init_w, init_h);
        js_engine_eval(js, buf, "<window-size-init>");
    }

    /* A failed script load skips the loop and takes the same teardown. */
    GameLoop *loop = scripts_ok ? game_loop_create(platform, js, 60) : NULL;
    if (!loop) {
        if (scripts_ok) fatal_error("Failed to create game loop");
        video_player_shutdown();
        effekseer_shutdown();
        input_manager_shutdown();
        if (!no_audio) audio_engine_shutdown();
        font_manager_shutdown();
        canvas2d_shutdown();
        image_loader_shutdown();
        {
            extern Renderer *s_renderer;
            if (s_renderer) { renderer_destroy(s_renderer); s_renderer = NULL; }
        }
        bind_filters_shutdown();
        filters_shutdown();
        js_engine_shutdown(js);
        platform_shutdown(platform);
        error_handler_shutdown();
        return 1;
    }

    if (perf_every > 0) {
        game_loop_set_perf_report(loop, perf_every);
    }
    if (max_frames > 0) {
        extern Renderer *s_renderer;
        game_loop_set_frame_limit(loop, max_frames, screenshot_path, &s_renderer);
        if (probe_expr) game_loop_set_exit_probe(loop, probe_expr);
    }
    game_loop_run(loop);

    game_loop_destroy(loop);
    video_player_shutdown();
    effekseer_shutdown();
    input_manager_shutdown();
    audio_engine_shutdown();
    font_manager_shutdown();
    canvas2d_shutdown();
    image_loader_shutdown();
    /* Clean up renderer and filter GPU resources before destroying GL context. */
    {
        extern Renderer *s_renderer;
        if (s_renderer) {
            renderer_destroy(s_renderer);
            s_renderer = NULL;
        }
    }
    bind_filters_shutdown();
    filters_shutdown();
    js_engine_shutdown(js);
    platform_shutdown(platform);
    error_handler_log(LOG_INFO, "Shutdown complete");
    error_handler_shutdown();
    return 0;
}
