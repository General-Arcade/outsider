/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/* Effekseer backend. With RMMZ_HAS_EFFEKSEER defined this wraps the Effekseer
   SDK and EffekseerRendererGL; otherwise a tracking stub records handles and
   play state without rendering (headless tests, or SDK not built). */

#include "effects/effekseer_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static bool s_initialized = false;
static int  s_screen_width = 0;
static int  s_screen_height = 0;

/* Stub backend */

#ifndef RMMZ_HAS_EFFEKSEER

#define MAX_EFFECTS    256
#define MAX_INSTANCES  512

typedef struct {
    bool    in_use;
    char   *path;
    float   scale;
    bool    loaded;
} StubEffect;

typedef struct {
    bool    in_use;
    uint32_t effect_handle;
    float   x, y, z;
    float   rx, ry, rz;
    float   sx, sy, sz;
    float   speed;
    bool    playing;
    float   lifetime;    /* frames remaining before the stub auto-expires it */
} StubInstance;

static StubEffect    s_effects[MAX_EFFECTS];
static StubInstance  s_instances[MAX_INSTANCES];
static uint32_t      s_next_effect_id = 1;
static int32_t       s_next_instance_id = 0;
static bool          s_restoration_flag = true;
static float         s_proj_matrix[16];
static float         s_camera_matrix[16];
static int           s_loaded_count = 0;
static int           s_playing_count = 0;

static void set_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

bool effekseer_init(int screen_width, int screen_height, int max_sprites)
{
    (void)max_sprites;
    if (s_initialized) return true;

    memset(s_effects, 0, sizeof(s_effects));
    memset(s_instances, 0, sizeof(s_instances));
    s_next_effect_id = 1;
    s_next_instance_id = 0;
    s_restoration_flag = true;
    s_loaded_count = 0;
    s_playing_count = 0;
    set_identity(s_proj_matrix);
    set_identity(s_camera_matrix);

    s_screen_width = screen_width;
    s_screen_height = screen_height;
    s_initialized = true;

    printf("effekseer: initialized (stub backend, %dx%d)\n",
           screen_width, screen_height);
    return true;
}

void effekseer_shutdown(void)
{
    if (!s_initialized) return;

    for (int i = 0; i < MAX_EFFECTS; i++) {
        if (s_effects[i].in_use) {
            free(s_effects[i].path);
            s_effects[i].in_use = false;
        }
    }
    memset(s_instances, 0, sizeof(s_instances));
    s_loaded_count = 0;
    s_playing_count = 0;
    s_initialized = false;
    printf("effekseer: shutdown (stub)\n");
}

bool effekseer_is_initialized(void)
{
    return s_initialized;
}

EffectHandle effekseer_load(const char *path, float scale)
{
    if (!s_initialized || !path) return EFFECT_HANDLE_INVALID;

    int slot = -1;
    for (int i = 0; i < MAX_EFFECTS; i++) {
        if (!s_effects[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        fprintf(stderr, "effekseer: too many loaded effects (max %d)\n", MAX_EFFECTS);
        return EFFECT_HANDLE_INVALID;
    }

    s_effects[slot].in_use = true;
    s_effects[slot].path = strdup(path);
    s_effects[slot].scale = scale;
    s_effects[slot].loaded = true;
    s_loaded_count++;

    /* Handle = slot + 1 (0 is invalid). */
    uint32_t handle = (uint32_t)(slot + 1);
    return handle;
}

void effekseer_release(EffectHandle handle)
{
    if (!s_initialized || handle == EFFECT_HANDLE_INVALID) return;
    int slot = (int)handle - 1;
    if (slot < 0 || slot >= MAX_EFFECTS || !s_effects[slot].in_use) return;

    /* Stop any instances using this effect. */
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].in_use && s_instances[i].effect_handle == handle) {
            if (s_instances[i].playing) {
                s_playing_count--;
            }
            s_instances[i].in_use = false;
            s_instances[i].playing = false;
        }
    }

    free(s_effects[slot].path);
    s_effects[slot].in_use = false;
    s_effects[slot].path = NULL;
    s_loaded_count--;
}

bool effekseer_is_loaded(EffectHandle handle)
{
    if (!s_initialized || handle == EFFECT_HANDLE_INVALID) return false;
    int slot = (int)handle - 1;
    if (slot < 0 || slot >= MAX_EFFECTS || !s_effects[slot].in_use) return false;
    return s_effects[slot].loaded;
}

EffectInstanceHandle effekseer_play(EffectHandle effect, float x, float y, float z)
{
    if (!s_initialized || effect == EFFECT_HANDLE_INVALID) return EFFECT_INSTANCE_INVALID;
    int eslot = (int)effect - 1;
    if (eslot < 0 || eslot >= MAX_EFFECTS || !s_effects[eslot].in_use) {
        return EFFECT_INSTANCE_INVALID;
    }

    int slot = -1;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (!s_instances[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        fprintf(stderr, "effekseer: too many playing instances (max %d)\n", MAX_INSTANCES);
        return EFFECT_INSTANCE_INVALID;
    }

    s_instances[slot].in_use = true;
    s_instances[slot].effect_handle = effect;
    s_instances[slot].x = x;
    s_instances[slot].y = y;
    s_instances[slot].z = z;
    s_instances[slot].rx = s_instances[slot].ry = s_instances[slot].rz = 0.0f;
    s_instances[slot].sx = s_instances[slot].sy = s_instances[slot].sz = 1.0f;
    s_instances[slot].speed = 1.0f;
    s_instances[slot].playing = true;
    s_instances[slot].lifetime = 300.0f;
    s_playing_count++;

    return (int32_t)slot;
}

void effekseer_stop(EffectInstanceHandle instance)
{
    if (!s_initialized || instance == EFFECT_INSTANCE_INVALID) return;
    if (instance < 0 || instance >= MAX_INSTANCES) return;
    if (!s_instances[instance].in_use) return;

    if (s_instances[instance].playing) {
        s_instances[instance].playing = false;
        s_playing_count--;
    }
    s_instances[instance].in_use = false;
}

void effekseer_stop_root(EffectInstanceHandle instance)
{
    effekseer_stop(instance);
}

void effekseer_stop_all(void)
{
    if (!s_initialized) return;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].in_use) {
            s_instances[i].playing = false;
            s_instances[i].in_use = false;
        }
    }
    s_playing_count = 0;
}

void effekseer_set_position(EffectInstanceHandle instance, float x, float y, float z)
{
    if (!s_initialized || instance < 0 || instance >= MAX_INSTANCES) return;
    if (!s_instances[instance].in_use) return;
    s_instances[instance].x = x;
    s_instances[instance].y = y;
    s_instances[instance].z = z;
}

void effekseer_set_rotation(EffectInstanceHandle instance, float x, float y, float z)
{
    if (!s_initialized || instance < 0 || instance >= MAX_INSTANCES) return;
    if (!s_instances[instance].in_use) return;
    s_instances[instance].rx = x;
    s_instances[instance].ry = y;
    s_instances[instance].rz = z;
}

void effekseer_set_scale(EffectInstanceHandle instance, float x, float y, float z)
{
    if (!s_initialized || instance < 0 || instance >= MAX_INSTANCES) return;
    if (!s_instances[instance].in_use) return;
    s_instances[instance].sx = x;
    s_instances[instance].sy = y;
    s_instances[instance].sz = z;
}

void effekseer_set_speed(EffectInstanceHandle instance, float speed)
{
    if (!s_initialized || instance < 0 || instance >= MAX_INSTANCES) return;
    if (!s_instances[instance].in_use) return;
    s_instances[instance].speed = speed;
}

bool effekseer_exists(EffectInstanceHandle instance)
{
    if (!s_initialized || instance < 0 || instance >= MAX_INSTANCES) return false;
    return s_instances[instance].in_use && s_instances[instance].playing;
}

void effekseer_set_projection_matrix(const float *matrix)
{
    if (!s_initialized || !matrix) return;
    memcpy(s_proj_matrix, matrix, 16 * sizeof(float));
}

void effekseer_set_camera_matrix(const float *matrix)
{
    if (!s_initialized || !matrix) return;
    memcpy(s_camera_matrix, matrix, 16 * sizeof(float));
}

void effekseer_update(float delta_frames)
{
    if (!s_initialized) return;

    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].in_use && s_instances[i].playing) {
            s_instances[i].lifetime -= delta_frames * s_instances[i].speed;
            if (s_instances[i].lifetime <= 0.0f) {
                s_instances[i].playing = false;
                s_instances[i].in_use = false;
                s_playing_count--;
            }
        }
    }
}

void effekseer_begin_draw(void)
{
    /* Stub: no-op. */
}

void effekseer_draw_handle(EffectInstanceHandle instance)
{
    (void)instance;
    /* Stub: no-op. */
}

void effekseer_end_draw(void)
{
    /* Stub: no-op. */
}

void effekseer_set_restoration_of_states_flag(bool flag)
{
    s_restoration_flag = flag;
}

void effekseer_set_background(uint32_t gl_texture)
{
    (void)gl_texture;
    /* Stub: no-op. */
}

void effekseer_reset_background(void)
{
    /* Stub: no-op. */
}

int effekseer_get_loaded_count(void)
{
    return s_loaded_count;
}

int effekseer_get_playing_count(void)
{
    return s_playing_count;
}

#else /* RMMZ_HAS_EFFEKSEER */

/* SDK backend */

#include <Effekseer.h>
#include <EffekseerRendererGL.h>

static ::Effekseer::ManagerRef s_manager;
static ::EffekseerRendererGL::RendererRef s_renderer;

/* EffectHandle = slot index + 1. */
#define MAX_EFFECTS 256
static ::Effekseer::EffectRef s_effect_refs[MAX_EFFECTS];
static uint32_t s_next_effect_id = 1;
static int s_loaded_count = 0;
static int s_playing_count = 0;
static bool s_restoration_flag = true;

/* Active instance handles, so s_playing_count can be reconciled after the SDK
   silently removes finished effects in Update(). */
#define MAX_ACTIVE_INSTANCES 256
static ::Effekseer::Handle s_active_instances[MAX_ACTIVE_INSTANCES];
static int s_active_instance_count = 0;

bool effekseer_init(int screen_width, int screen_height, int max_sprites)
{
    if (s_initialized) return true;

    s_renderer = ::EffekseerRendererGL::Renderer::Create(
        max_sprites,
        ::EffekseerRendererGL::OpenGLDeviceType::OpenGL3);
    if (!s_renderer) {
        fprintf(stderr, "effekseer: failed to create GL renderer\n");
        return false;
    }

    s_manager = ::Effekseer::Manager::Create(max_sprites);
    if (!s_manager) {
        fprintf(stderr, "effekseer: failed to create manager\n");
        s_renderer.Reset();
        return false;
    }

    s_manager->SetSpriteRenderer(s_renderer->CreateSpriteRenderer());
    s_manager->SetRibbonRenderer(s_renderer->CreateRibbonRenderer());
    s_manager->SetRingRenderer(s_renderer->CreateRingRenderer());
    s_manager->SetTrackRenderer(s_renderer->CreateTrackRenderer());
    s_manager->SetModelRenderer(s_renderer->CreateModelRenderer());

    s_manager->SetTextureLoader(s_renderer->CreateTextureLoader());
    s_manager->SetModelLoader(s_renderer->CreateModelLoader());
    s_manager->SetMaterialLoader(s_renderer->CreateMaterialLoader());
    s_manager->SetCurveLoader(Effekseer::MakeRefPtr<Effekseer::CurveLoader>());

    memset(s_effect_refs, 0, sizeof(s_effect_refs));
    s_next_effect_id = 1;
    s_loaded_count = 0;
    s_playing_count = 0;
    s_active_instance_count = 0;

    s_screen_width = screen_width;
    s_screen_height = screen_height;
    s_initialized = true;

    printf("effekseer: initialized (real SDK, %dx%d, max_sprites=%d)\n",
           screen_width, screen_height, max_sprites);
    return true;
}

void effekseer_shutdown(void)
{
    if (!s_initialized) return;

    for (int i = 0; i < MAX_EFFECTS; i++) {
        s_effect_refs[i].Reset();
    }
    s_manager.Reset();
    s_renderer.Reset();
    s_loaded_count = 0;
    s_playing_count = 0;
    s_active_instance_count = 0;
    s_initialized = false;
    printf("effekseer: shutdown (real SDK)\n");
}

bool effekseer_is_initialized(void)
{
    return s_initialized;
}

EffectHandle effekseer_load(const char *path, float scale)
{
    if (!s_initialized || !path) return EFFECT_HANDLE_INVALID;

    int slot = -1;
    for (int i = 0; i < MAX_EFFECTS; i++) {
        if (!s_effect_refs[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return EFFECT_HANDLE_INVALID;

    /* Effekseer takes char16_t paths; widen byte-wise (ASCII paths only). */
    size_t len = strlen(path);
    char16_t *wpath = new char16_t[len + 1];
    for (size_t i = 0; i <= len; i++) {
        wpath[i] = (char16_t)(unsigned char)path[i];
    }

    auto effect = ::Effekseer::Effect::Create(s_manager, wpath, scale);
    delete[] wpath;

    if (!effect) return EFFECT_HANDLE_INVALID;

    s_effect_refs[slot] = effect;
    s_loaded_count++;
    return (uint32_t)(slot + 1);
}

void effekseer_release(EffectHandle handle)
{
    if (!s_initialized || handle == EFFECT_HANDLE_INVALID) return;
    int slot = (int)handle - 1;
    if (slot < 0 || slot >= MAX_EFFECTS || !s_effect_refs[slot]) return;

    s_effect_refs[slot].Reset();
    s_loaded_count--;
}

bool effekseer_is_loaded(EffectHandle handle)
{
    if (!s_initialized || handle == EFFECT_HANDLE_INVALID) return false;
    int slot = (int)handle - 1;
    if (slot < 0 || slot >= MAX_EFFECTS) return false;
    return !!s_effect_refs[slot];
}

EffectInstanceHandle effekseer_play(EffectHandle effect, float x, float y, float z)
{
    if (!s_initialized || effect == EFFECT_HANDLE_INVALID) return EFFECT_INSTANCE_INVALID;
    int slot = (int)effect - 1;
    if (slot < 0 || slot >= MAX_EFFECTS || !s_effect_refs[slot]) {
        return EFFECT_INSTANCE_INVALID;
    }

    auto handle = s_manager->Play(s_effect_refs[slot], x, y, z);
    s_playing_count++;
    if (s_active_instance_count < MAX_ACTIVE_INSTANCES) {
        s_active_instances[s_active_instance_count++] = handle;
    }
    return (EffectInstanceHandle)handle;
}

static void remove_active_instance(::Effekseer::Handle h)
{
    for (int i = 0; i < s_active_instance_count; i++) {
        if (s_active_instances[i] == h) {
            s_active_instances[i] = s_active_instances[--s_active_instance_count];
            return;
        }
    }
}

void effekseer_stop(EffectInstanceHandle instance)
{
    if (!s_initialized) return;
    if (s_manager->Exists((Effekseer::Handle)instance)) {
        s_playing_count--;
    }
    remove_active_instance((Effekseer::Handle)instance);
    s_manager->StopEffect((Effekseer::Handle)instance);
}

void effekseer_stop_root(EffectInstanceHandle instance)
{
    if (!s_initialized) return;
    if (s_manager->Exists((Effekseer::Handle)instance)) {
        s_playing_count--;
    }
    remove_active_instance((Effekseer::Handle)instance);
    s_manager->StopRoot((Effekseer::Handle)instance);
}

void effekseer_stop_all(void)
{
    if (!s_initialized) return;
    s_manager->StopAllEffects();
    s_playing_count = 0;
    s_active_instance_count = 0;
}

void effekseer_set_position(EffectInstanceHandle instance, float x, float y, float z)
{
    if (!s_initialized) return;
    s_manager->SetLocation((Effekseer::Handle)instance, x, y, z);
}

void effekseer_set_rotation(EffectInstanceHandle instance, float x, float y, float z)
{
    if (!s_initialized) return;
    s_manager->SetRotation((Effekseer::Handle)instance, x, y, z);
}

void effekseer_set_scale(EffectInstanceHandle instance, float x, float y, float z)
{
    if (!s_initialized) return;
    s_manager->SetScale((Effekseer::Handle)instance, x, y, z);
}

void effekseer_set_speed(EffectInstanceHandle instance, float speed)
{
    if (!s_initialized) return;
    s_manager->SetSpeed((Effekseer::Handle)instance, speed);
}

bool effekseer_exists(EffectInstanceHandle instance)
{
    if (!s_initialized) return false;
    return s_manager->Exists((Effekseer::Handle)instance);
}

void effekseer_set_projection_matrix(const float *matrix)
{
    if (!s_initialized || !matrix) return;
    Effekseer::Matrix44 m;
    memcpy(m.Values, matrix, 16 * sizeof(float));
    s_renderer->SetProjectionMatrix(m);
}

void effekseer_set_camera_matrix(const float *matrix)
{
    if (!s_initialized || !matrix) return;
    Effekseer::Matrix44 m;
    memcpy(m.Values, matrix, 16 * sizeof(float));
    s_renderer->SetCameraMatrix(m);
}

void effekseer_update(float delta_frames)
{
    if (!s_initialized) return;
    s_manager->Update(delta_frames);

    /* Drop instances the SDK has finished and removed. */
    for (int i = s_active_instance_count - 1; i >= 0; i--) {
        if (!s_manager->Exists(s_active_instances[i])) {
            s_playing_count--;
            s_active_instances[i] = s_active_instances[--s_active_instance_count];
        }
    }
}

void effekseer_begin_draw(void)
{
    if (!s_initialized) return;
    s_renderer->SetRestorationOfStatesFlag(s_restoration_flag);
    s_renderer->BeginRendering();
}

void effekseer_draw_handle(EffectInstanceHandle instance)
{
    if (!s_initialized) return;
    s_manager->DrawHandle((Effekseer::Handle)instance);
}

void effekseer_end_draw(void)
{
    if (!s_initialized) return;
    s_renderer->EndRendering();
}

void effekseer_set_restoration_of_states_flag(bool flag)
{
    s_restoration_flag = flag;
}

void effekseer_set_background(uint32_t gl_texture)
{
    if (!s_initialized) return;
    auto bg = EffekseerRendererGL::CreateTexture(
        s_renderer->GetGraphicsDevice(), gl_texture, false, nullptr);
    s_renderer->SetBackground(bg);
}

void effekseer_reset_background(void)
{
    if (!s_initialized) return;
    s_renderer->SetBackground(nullptr);
}

int effekseer_get_loaded_count(void)
{
    return s_loaded_count;
}

int effekseer_get_playing_count(void)
{
    if (!s_initialized) return 0;
    /* Tracked count, reconciled in effekseer_update(). */
    return s_playing_count;
}

#endif /* RMMZ_HAS_EFFEKSEER */
