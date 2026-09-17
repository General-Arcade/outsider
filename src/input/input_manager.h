/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_INPUT_MANAGER_H
#define RMMZ_INPUT_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/* Maximum queued events per frame. */
#define INPUT_MAX_EVENTS 256

/* Gamepad constants matching the W3C Standard Gamepad mapping. */
#define INPUT_MAX_GAMEPADS       4
#define INPUT_GAMEPAD_BUTTONS   17
#define INPUT_GAMEPAD_AXES       4

typedef enum {
    INPUT_KEY_DOWN,
    INPUT_KEY_UP,
    INPUT_MOUSE_DOWN,
    INPUT_MOUSE_UP,
    INPUT_MOUSE_MOVE,
    INPUT_WHEEL,
    INPUT_TEXT,         /* committed text from the OS (SDL_TEXTINPUT) */
} InputEventType;

/* Platform-independent input event with DOM-style field values
   (the platform layer does the SDL -> DOM conversion). */
typedef struct {
    InputEventType type;

    /* Keyboard (INPUT_KEY_DOWN / INPUT_KEY_UP); INPUT_TEXT carries the
       typed UTF-8 text in `key` (layout- and IME-aware, unlike keyCode). */
    int  keyCode;       /* DOM keyCode */
    char key[32];       /* DOM key, e.g. "Enter", "a" */
    char code[32];      /* DOM code, e.g. "KeyA", "Space" */
    bool repeat;
    bool shiftKey;
    bool ctrlKey;
    bool altKey;
    bool metaKey;

    /* Mouse (INPUT_MOUSE_DOWN / UP / MOVE / WHEEL) */
    int mouseX;
    int mouseY;
    int button;         /* 0 = left, 1 = middle, 2 = right */

    /* Wheel (INPUT_WHEEL) */
    double wheelDeltaX;
    double wheelDeltaY;
} InputEvent;

/* Gamepad state (polled each frame, not event-driven). */

typedef struct {
    bool   pressed;
    double value;       /* 0.0 or 1.0 for digital, analog value otherwise */
} GamepadButton;

typedef struct {
    bool          connected;
    char          id[128];
    int           index;
    GamepadButton buttons[INPUT_GAMEPAD_BUTTONS];
    double        axes[INPUT_GAMEPAD_AXES];
} GamepadState;

/* Initialize the input manager. Safe to call multiple times. */
void input_manager_init(void);

/* Shut down the input manager. Safe to call when uninitialized. */
void input_manager_shutdown(void);

/* Queue an event (called by the platform layer). Dropped if the queue is full. */
void input_manager_push_event(const InputEvent *event);

/* Copy up to max_events queued events into out[] and clear the queue.
   Returns the number copied. */
int input_manager_poll_events(InputEvent *out, int max_events);

/* Update gamepad state for slot 0-3 (called by the platform layer each frame). */
void input_manager_set_gamepad(int index, const GamepadState *state);

/* Clear a gamepad slot (marks it disconnected). */
void input_manager_clear_gamepad(int index);

/* Get a gamepad slot's state, or NULL if out of range / uninitialized. */
const GamepadState *input_manager_get_gamepad(int index);

#endif /* RMMZ_INPUT_MANAGER_H */
