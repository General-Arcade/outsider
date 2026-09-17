/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "video/video_player.h"
#include "audio/audio_engine.h"
#include "io/file_io.h"
#include "rendering/renderer.h"
#include "rendering/sprite_batch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* pl_mpeg's declarations use FILE, so stdio.h must come first. */
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

#define MAX_VIDEOS 4

/* Decoding is driven by wall-clock time. A single huge step (window drag,
   debugger) would otherwise decode seconds of frames at once. */
#define MAX_STEP_SECONDS 0.25

/* Audio is decoded this far ahead of the video clock so the mixer's ring
   buffer never runs dry between frames. */
#define AUDIO_LEAD_SECONDS 0.15

typedef struct {
    bool              in_use;
    plm_t            *plm;
    int               width, height;
    uint8_t          *rgba;          /* latest decoded frame */
    bool              frame_dirty;   /* rgba changed since last upload */
    uint32_t          texture;
    int               frame_count;
    bool              playing;
    bool              paused;
    bool              visible;
    bool              loop;
    float             volume;
    AudioStreamHandle audio;
} Video;

static Video      s_videos[MAX_VIDEOS + 1];   /* index 0 unused */
static Renderer **s_renderer_slot = NULL;

static Video *get_video(VideoHandle h)
{
    if (h <= 0 || h > MAX_VIDEOS || !s_videos[h].in_use) return NULL;
    return &s_videos[h];
}

static void on_video_frame(plm_t *plm, plm_frame_t *frame, void *user)
{
    (void)plm;
    Video *v = user;
    plm_frame_to_rgba(frame, v->rgba, v->width * 4);
    v->frame_dirty = true;
    v->frame_count++;
}

static void on_audio_samples(plm_t *plm, plm_samples_t *samples, void *user)
{
    (void)plm;
    Video *v = user;
    if (v->audio != AUDIO_STREAM_INVALID) {
        audio_stream_push(v->audio, samples->interleaved, (int)samples->count);
    }
}

void video_player_init(Renderer **renderer_slot)
{
    memset(s_videos, 0, sizeof(s_videos));
    s_renderer_slot = renderer_slot;
}

void video_player_shutdown(void)
{
    for (int i = 1; i <= MAX_VIDEOS; i++) {
        if (s_videos[i].in_use) video_close(i);
    }
    s_renderer_slot = NULL;
}

VideoHandle video_open(const char *path)
{
    if (!path) return VIDEO_HANDLE_INVALID;

    char resolved[1024];
    if (!file_io_resolve_path(path, resolved, sizeof(resolved))) return VIDEO_HANDLE_INVALID;

    int slot = 0;
    for (int i = 1; i <= MAX_VIDEOS; i++) {
        if (!s_videos[i].in_use) { slot = i; break; }
    }
    if (!slot) {
        fprintf(stderr, "video: too many open videos\n");
        return VIDEO_HANDLE_INVALID;
    }

    plm_t *plm = plm_create_with_filename(resolved);
    if (!plm) return VIDEO_HANDLE_INVALID;

    int w = plm_get_width(plm), h = plm_get_height(plm);
    if (w <= 0 || h <= 0) {
        plm_destroy(plm);
        return VIDEO_HANDLE_INVALID;
    }

    Video *v = &s_videos[slot];
    memset(v, 0, sizeof(*v));
    v->in_use = true;
    v->plm = plm;
    v->width = w;
    v->height = h;
    v->rgba = calloc((size_t)w * (size_t)h * 4, 1);
    if (!v->rgba) {
        plm_destroy(plm);
        v->in_use = false;
        return VIDEO_HANDLE_INVALID;
    }
    /* plm_frame_to_rgba() never touches the alpha byte; the sprite shader
       premultiplies by it, so set every pixel opaque once. */
    for (size_t i = 3; i < (size_t)w * (size_t)h * 4; i += 4) v->rgba[i] = 0xFF;
    v->visible = true;
    v->volume = 1.0f;
    v->audio = AUDIO_STREAM_INVALID;

    plm_set_video_decode_callback(plm, on_video_frame, v);
    plm_set_audio_decode_callback(plm, on_audio_samples, v);
    plm_set_audio_lead_time(plm, AUDIO_LEAD_SECONDS);
    plm_set_loop(plm, 0);
    return slot;
}

void video_close(VideoHandle h)
{
    Video *v = get_video(h);
    if (!v) return;
    if (v->audio != AUDIO_STREAM_INVALID) audio_stream_destroy(v->audio);
    if (v->texture) renderer_delete_texture(v->texture);
    plm_destroy(v->plm);
    free(v->rgba);
    memset(v, 0, sizeof(*v));
}

void video_play(VideoHandle h)
{
    Video *v = get_video(h);
    if (!v) return;
    if (plm_has_ended(v->plm)) return;
    if (v->audio == AUDIO_STREAM_INVALID && plm_get_num_audio_streams(v->plm) > 0) {
        v->audio = audio_stream_create(plm_get_samplerate(v->plm), 2);
        if (v->audio == AUDIO_STREAM_INVALID) {
            /* No audio engine: decode video only. */
            plm_set_audio_enabled(v->plm, 0);
        } else {
            audio_stream_set_volume(v->audio, v->volume);
        }
    }
    v->playing = true;
    v->paused = false;
    if (v->audio != AUDIO_STREAM_INVALID) audio_stream_set_paused(v->audio, false);
}

void video_pause(VideoHandle h)
{
    Video *v = get_video(h);
    if (!v) return;
    v->paused = true;
    if (v->audio != AUDIO_STREAM_INVALID) audio_stream_set_paused(v->audio, true);
}

void video_set_volume(VideoHandle h, float volume)
{
    Video *v = get_video(h);
    if (!v) return;
    v->volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    if (v->audio != AUDIO_STREAM_INVALID) audio_stream_set_volume(v->audio, v->volume);
}

void video_set_loop(VideoHandle h, bool loop)
{
    Video *v = get_video(h);
    if (!v) return;
    v->loop = loop;
    plm_set_loop(v->plm, loop ? 1 : 0);
}

void video_set_visible(VideoHandle h, bool visible)
{
    Video *v = get_video(h);
    if (v) v->visible = visible;
}

bool video_get_state(VideoHandle h, VideoState *out)
{
    Video *v = get_video(h);
    if (!v || !out) return false;
    bool ended = plm_has_ended(v->plm) != 0;
    out->playing = v->playing && !v->paused && !ended;
    out->paused = v->paused;
    out->ended = ended;
    out->time = plm_get_time(v->plm);
    out->duration = plm_get_duration(v->plm);
    out->width = v->width;
    out->height = v->height;
    return true;
}

int video_get_frame_count(VideoHandle h)
{
    Video *v = get_video(h);
    return v ? v->frame_count : 0;
}

void video_player_update(double dt_seconds)
{
    if (dt_seconds <= 0.0) return;
    if (dt_seconds > MAX_STEP_SECONDS) dt_seconds = MAX_STEP_SECONDS;

    for (int i = 1; i <= MAX_VIDEOS; i++) {
        Video *v = &s_videos[i];
        if (!v->in_use || !v->playing || v->paused) continue;
        if (plm_has_ended(v->plm)) {
            v->playing = false;
            continue;
        }
        plm_decode(v->plm, dt_seconds);
    }
}

void video_player_draw(void)
{
    Renderer *r = s_renderer_slot ? *s_renderer_slot : NULL;
    if (!r) return;

    for (int i = 1; i <= MAX_VIDEOS; i++) {
        Video *v = &s_videos[i];
        if (!v->in_use || !v->visible || !v->playing || v->frame_count == 0) continue;
        /* Keep showing the last frame while paused (e.g. mid-skip) but not
           once the movie has ended: the game shows itself again then. */
        if (plm_has_ended(v->plm) && !v->loop) continue;

        if (!v->texture) {
            v->texture = renderer_create_texture(v->width, v->height);
            if (!v->texture) continue;
            renderer_set_texture_filter(v->texture, true);
            v->frame_dirty = true;
        }
        if (v->frame_dirty) {
            renderer_update_texture(v->texture, v->width, v->height, v->rgba);
            v->frame_dirty = false;
        }

        /* Letterbox the movie inside the game area, like the browser's
           video element centred over the canvas. */
        int gw = 0, gh = 0;
        renderer_get_size(r, &gw, &gh);
        if (gw <= 0 || gh <= 0) continue;
        float scale_x = (float)gw / (float)v->width;
        float scale_y = (float)gh / (float)v->height;
        float scale = scale_x < scale_y ? scale_x : scale_y;
        float dw = (float)v->width * scale, dh = (float)v->height * scale;
        float dx = ((float)gw - dw) * 0.5f, dy = ((float)gh - dh) * 0.5f;

        renderer_unbind_fbo(r);
        SpriteBatch *sb = renderer_get_batch(r);
        sprite_batch_begin(sb);
        sprite_batch_set_blend_mode(sb, BLEND_MODE_NORMAL);
        sprite_batch_draw(sb, 0, 0.0f, 0.0f, (float)gw, (float)gh,
                          0.0f, 0.0f, 1.0f, 1.0f, 0xFF000000u, 1.0f);
        sprite_batch_draw(sb, v->texture, dx, dy, dw, dh,
                          0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu, 1.0f);
        sprite_batch_end(sb);
    }
}
