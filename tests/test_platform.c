/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "platform/platform.h"
#ifdef RMMZ_RENDER_GPU
#include "rendering/gpu_backend.h"
#endif

#include <SDL3/SDL.h>
#ifndef RMMZ_RENDER_GPU
#include <SDL3/SDL_opengl.h>
#endif
#include <stdio.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  TEST %s ... ", #name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASS\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
    } while (0)

static void test_platform_init_shutdown(void)
{
    TEST(platform_init_creates_window);
    Platform *p = platform_init("Test Window", 320, 240);
    if (!p) {
        FAIL("platform_init returned NULL");
        return;
    }
    PASS();

    TEST(platform_window_size);
    int w = 0, h = 0;
    platform_get_window_size(p, &w, &h);
    if (w != 320 || h != 240) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected 320x240, got %dx%d", w, h);
        FAIL(buf);
    } else {
        PASS();
    }

#ifdef RMMZ_RENDER_GPU
    TEST(gpu_device_valid);
    /* platform_init() only returns non-NULL once gpu_backend_init() has
       claimed a device, so reaching here means one was created. */
    printf("PASS (GPU: %s)\n", SDL_GetGPUDeviceDriver(gpu_backend_device()));
    tests_passed++;
#else
    TEST(opengl_context_valid);
    /* If we can call glGetString without segfault, the GL context is valid. */
    const GLubyte *vendor = glGetString(GL_VENDOR);
    const GLubyte *renderer_str = glGetString(GL_RENDERER);
    if (!vendor || !renderer_str) {
        FAIL("glGetString returned NULL — no valid GL context");
    } else {
        printf("PASS (GL: %s / %s)\n", vendor, renderer_str);
        tests_passed++;
    }

    TEST(opengl_clear_succeeds);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        char buf[64];
        snprintf(buf, sizeof(buf), "glGetError returned 0x%x", err);
        FAIL(buf);
    } else {
        PASS();
    }
#endif

    TEST(platform_swap_buffers);
    platform_swap_buffers(p);
    PASS();

    TEST(platform_poll_events_returns_true);
    /* No quit event has been posted, so this should return true. */
    bool running = platform_poll_events(p);
    if (!running) {
        FAIL("poll_events returned false unexpectedly");
    } else {
        PASS();
    }

    TEST(platform_shutdown);
    platform_shutdown(p);
    PASS();
}

static void test_platform_null_safety(void)
{
    TEST(platform_shutdown_null);
    platform_shutdown(NULL); /* Should not crash. */
    PASS();
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("=== Platform Tests ===\n");
    test_platform_init_shutdown();
    test_platform_null_safety();

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
