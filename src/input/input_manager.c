/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "input/input_manager.h"
#include <string.h>

static struct {
    bool         initialized;
    InputEvent   events[INPUT_MAX_EVENTS];
    int          event_count;
    GamepadState gamepads[INPUT_MAX_GAMEPADS];
} g_input;

void input_manager_init(void)
{
    memset(&g_input, 0, sizeof(g_input));
    g_input.initialized = true;
}

void input_manager_shutdown(void)
{
    memset(&g_input, 0, sizeof(g_input));
}

/* Event queue */

void input_manager_push_event(const InputEvent *event)
{
    if (!g_input.initialized || !event) return;
    if (g_input.event_count >= INPUT_MAX_EVENTS) return;

    g_input.events[g_input.event_count++] = *event;
}

int input_manager_poll_events(InputEvent *out, int max_events)
{
    if (!g_input.initialized || !out || max_events <= 0) return 0;

    int count = g_input.event_count;
    if (count > max_events) count = max_events;

    memcpy(out, g_input.events, count * sizeof(InputEvent));
    g_input.event_count = 0;
    return count;
}

/* Gamepad state */

void input_manager_set_gamepad(int index, const GamepadState *state)
{
    if (!g_input.initialized || !state) return;
    if (index < 0 || index >= INPUT_MAX_GAMEPADS) return;

    g_input.gamepads[index] = *state;
}

void input_manager_clear_gamepad(int index)
{
    if (!g_input.initialized) return;
    if (index < 0 || index >= INPUT_MAX_GAMEPADS) return;

    memset(&g_input.gamepads[index], 0, sizeof(GamepadState));
}

const GamepadState *input_manager_get_gamepad(int index)
{
    if (!g_input.initialized) return NULL;
    if (index < 0 || index >= INPUT_MAX_GAMEPADS) return NULL;

    return &g_input.gamepads[index];
}
