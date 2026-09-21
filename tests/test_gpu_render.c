/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/* End-to-end tests for the SDL3 GPU backend, run without a window.
 *
 * Every other test builds the rendering sources in their headless stub form,
 * so nothing has actually checked that drawing produces the right pixels.
 * These do: a real device, real shaders, real draws, read back and compared.
 *
 * A machine with no usable GPU backend reports the tests as skipped rather
 * than failing, so this can run anywhere.
 */

#include "rendering/gpu_backend.h"
#include "rendering/renderer.h"
#include "rendering/sprite_batch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests = 0;
static int g_failures = 0;
static const char *g_current = "";

#define TEST(name) do { g_current = #name; g_tests++; } while (0)
#define PASS() printf("  PASS  %s\n", g_current)
#define FAIL(msg) do { \
    printf("  FAIL  %s: %s\n", g_current, msg); \
    g_failures++; \
} while (0)
#define CHECK(cond, msg) do { if (cond) { PASS(); } else { FAIL(msg); } } while (0)

#define W 64
#define H 48

/* renderer_read_pixels returns rows top-down, four bytes per pixel. */
static void pixel_at(const uint8_t *px, int x, int y, uint8_t out[4])
{
    memcpy(out, px + ((size_t)y * W + x) * 4, 4);
}

static bool near_enough(uint8_t got, int want)
{
    int diff = (int)got - want;
    return diff <= 2 && diff >= -2;
}

static void test_clear_and_draw(void)
{
    Renderer *r = renderer_create(W, H);
    if (!r) { TEST(renderer_create); FAIL("renderer_create returned NULL"); return; }
    renderer_set_output_size(r, W, H);
    renderer_set_screen_viewport(r, 0, 0, W, H);

    /* A frame that clears to opaque black and draws one white quad over the
       left half. The batch's untextured path supplies a white texel, so the
       quad's colour is its tint. */
    renderer_begin_frame(r);
    SpriteBatch *sb = renderer_get_batch(r);
    sprite_batch_draw(sb, 0, 0.0f, 0.0f, (float)(W / 2), (float)H,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFF0000, 1.0f);
    renderer_end_frame(r);

    uint8_t *px = renderer_read_pixels(r, 0, W, H);
    if (!px) { TEST(read_pixels); FAIL("read_pixels returned NULL"); renderer_destroy(r); return; }

    uint8_t left[4], right[4];
    pixel_at(px, W / 4, H / 2, left);
    pixel_at(px, (W * 3) / 4, H / 2, right);

    TEST(tinted_quad_covers_its_half);
    {
        char msg[96];
        if (near_enough(left[0], 255) && near_enough(left[1], 0) &&
            near_enough(left[2], 0) && near_enough(left[3], 255)) {
            PASS();
        } else {
            snprintf(msg, sizeof(msg), "left pixel was %u,%u,%u,%u",
                     left[0], left[1], left[2], left[3]);
            FAIL(msg);
        }
    }

    TEST(the_rest_of_the_frame_is_the_clear_colour);
    {
        char msg[96];
        if (near_enough(right[0], 0) && near_enough(right[1], 0) &&
            near_enough(right[2], 0) && near_enough(right[3], 255)) {
            PASS();
        } else {
            snprintf(msg, sizeof(msg), "right pixel was %u,%u,%u,%u",
                     right[0], right[1], right[2], right[3]);
            FAIL(msg);
        }
    }

    free(px);
    renderer_destroy(r);
}

static void test_render_target_orientation(void)
{
    Renderer *r = renderer_create(W, H);
    if (!r) { TEST(renderer_create_for_target); FAIL("renderer_create returned NULL"); return; }
    renderer_set_output_size(r, W, H);

    uint32_t texture = 0;
    uint32_t fbo = renderer_create_fbo(W, H, &texture);
    if (!fbo) {
        TEST(create_render_target);
        FAIL("renderer_create_fbo returned 0");
        renderer_destroy(r);
        return;
    }

    /* Fill only the top quarter in game-space coordinates. Reading back gives
       rows top-down, so that band must land at the top of the image -- this is
       the orientation the JS layer depends on, and getting it wrong renders
       everything upside down. */
    renderer_bind_fbo(r, fbo, W, H);
    renderer_begin_frame_transparent(r);
    sprite_batch_draw(renderer_get_batch(r), 0, 0.0f, 0.0f, (float)W, (float)(H / 4),
                      0.0f, 0.0f, 1.0f, 1.0f, 0x00FF00, 1.0f);
    renderer_end_frame(r);
    renderer_unbind_fbo(r);

    uint8_t *px = renderer_read_pixels(r, fbo, W, H);
    if (!px) {
        TEST(read_render_target);
        FAIL("read_pixels returned NULL");
        renderer_delete_fbo(fbo, texture);
        renderer_destroy(r);
        return;
    }

    uint8_t top[4], bottom[4];
    pixel_at(px, W / 2, H / 8, top);          /* inside the band */
    pixel_at(px, W / 2, (H * 3) / 4, bottom); /* well below it */

    TEST(game_space_top_reads_back_as_the_top_row);
    {
        char msg[96];
        if (top[1] > 200 && top[3] > 200) {
            PASS();
        } else {
            snprintf(msg, sizeof(msg), "top pixel was %u,%u,%u,%u",
                     top[0], top[1], top[2], top[3]);
            FAIL(msg);
        }
    }

    TEST(the_untouched_part_stays_transparent);
    {
        char msg[96];
        if (bottom[3] < 8) {
            PASS();
        } else {
            snprintf(msg, sizeof(msg), "bottom alpha was %u", bottom[3]);
            FAIL(msg);
        }
    }

    free(px);
    renderer_delete_fbo(fbo, texture);
    renderer_destroy(r);
}

int main(void)
{
    printf("GPU renderer tests (windowless)\n");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("  SKIP  SDL video unavailable: %s\n", SDL_GetError());
        return 0;
    }
    if (!gpu_backend_init(NULL)) {
        printf("  SKIP  no usable GPU backend on this machine\n");
        SDL_Quit();
        return 0;
    }
    gpu_frame_resize(W, H);

    test_clear_and_draw();
    test_render_target_orientation();

    gpu_backend_shutdown();
    SDL_Quit();

    printf("\n%d tests, %d failures\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}
