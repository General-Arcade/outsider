/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "platform/platform.h"
#include "input/input_manager.h"

#include <SDL.h>
#include "rendering/gl_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Platform {
    SDL_Window   *window;
    SDL_GLContext  gl_ctx;

    SDL_GameController *controllers[INPUT_MAX_GAMEPADS];
    SDL_JoystickID      controller_ids[INPUT_MAX_GAMEPADS];

    /* Set when SDL_WINDOWEVENT_RESIZED fires; consumed by platform_check_resize. */
    bool  resize_pending;
    int   resize_w;
    int   resize_h;
};

/* SDL keycode -> DOM keyCode */

static int sdl_to_dom_keycode(SDL_Keycode key)
{
    /* Letters (DOM uses the uppercase ASCII code). */
    if (key >= SDLK_a && key <= SDLK_z)
        return (key - SDLK_a) + 65;

    if (key >= SDLK_0 && key <= SDLK_9)
        return key;

    /* F1..F12 -> 112..123 */
    if (key >= SDLK_F1 && key <= SDLK_F12)
        return 112 + (int)(key - SDLK_F1);

    /* Numpad 0..9 -> 96..105 */
    if (key >= SDLK_KP_0 && key <= SDLK_KP_9)
        return 96 + (int)(key - SDLK_KP_0);

    switch (key) {
        case SDLK_RETURN:       return 13;
        case SDLK_KP_ENTER:     return 13;
        case SDLK_ESCAPE:       return 27;
        case SDLK_BACKSPACE:    return 8;
        case SDLK_TAB:          return 9;
        case SDLK_SPACE:        return 32;
        case SDLK_LEFT:         return 37;
        case SDLK_UP:           return 38;
        case SDLK_RIGHT:        return 39;
        case SDLK_DOWN:         return 40;
        case SDLK_PAGEUP:       return 33;
        case SDLK_PAGEDOWN:     return 34;
        case SDLK_HOME:         return 36;
        case SDLK_END:          return 35;
        case SDLK_INSERT:       return 45;
        case SDLK_DELETE:       return 46;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:       return 16;
        case SDLK_LCTRL:
        case SDLK_RCTRL:        return 17;
        case SDLK_LALT:
        case SDLK_RALT:         return 18;
        case SDLK_LGUI:
        case SDLK_RGUI:         return 91;
        case SDLK_CAPSLOCK:     return 20;
        case SDLK_NUMLOCKCLEAR: return 144;
        case SDLK_SCROLLLOCK:   return 145;
        case SDLK_PAUSE:        return 19;
        case SDLK_PRINTSCREEN:  return 44;
        case SDLK_KP_MULTIPLY:  return 106;
        case SDLK_KP_PLUS:      return 107;
        case SDLK_KP_MINUS:     return 109;
        case SDLK_KP_DECIMAL:   return 110;
        case SDLK_KP_DIVIDE:    return 111;
        case SDLK_SEMICOLON:    return 186;
        case SDLK_EQUALS:       return 187;
        case SDLK_COMMA:        return 188;
        case SDLK_MINUS:        return 189;
        case SDLK_PERIOD:       return 190;
        case SDLK_SLASH:        return 191;
        case SDLK_BACKQUOTE:    return 192;
        case SDLK_LEFTBRACKET:  return 219;
        case SDLK_BACKSLASH:    return 220;
        case SDLK_RIGHTBRACKET: return 221;
        case SDLK_QUOTE:        return 222;
        default:                return 0;
    }
}

/* SDL keycode -> DOM "key" string */

static const char *sdl_to_dom_key(SDL_Keycode key, uint16_t mod)
{
    static char buf[8];

    if (key >= SDLK_a && key <= SDLK_z) {
        buf[0] = (mod & KMOD_SHIFT) ? (char)('A' + (key - SDLK_a))
                                     : (char)('a' + (key - SDLK_a));
        buf[1] = '\0';
        return buf;
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        buf[0] = (char)key;
        buf[1] = '\0';
        return buf;
    }

    switch (key) {
        case SDLK_RETURN:       return "Enter";
        case SDLK_KP_ENTER:     return "Enter";
        case SDLK_ESCAPE:       return "Escape";
        case SDLK_BACKSPACE:    return "Backspace";
        case SDLK_TAB:          return "Tab";
        case SDLK_SPACE:        return " ";
        case SDLK_LEFT:         return "ArrowLeft";
        case SDLK_UP:           return "ArrowUp";
        case SDLK_RIGHT:        return "ArrowRight";
        case SDLK_DOWN:         return "ArrowDown";
        case SDLK_PAGEUP:       return "PageUp";
        case SDLK_PAGEDOWN:     return "PageDown";
        case SDLK_HOME:         return "Home";
        case SDLK_END:          return "End";
        case SDLK_INSERT:       return "Insert";
        case SDLK_DELETE:       return "Delete";
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:       return "Shift";
        case SDLK_LCTRL:
        case SDLK_RCTRL:        return "Control";
        case SDLK_LALT:
        case SDLK_RALT:         return "Alt";
        case SDLK_LGUI:
        case SDLK_RGUI:         return "Meta";
        case SDLK_CAPSLOCK:     return "CapsLock";
        case SDLK_F1:           return "F1";
        case SDLK_F2:           return "F2";
        case SDLK_F3:           return "F3";
        case SDLK_F4:           return "F4";
        case SDLK_F5:           return "F5";
        case SDLK_F6:           return "F6";
        case SDLK_F7:           return "F7";
        case SDLK_F8:           return "F8";
        case SDLK_F9:           return "F9";
        case SDLK_F10:          return "F10";
        case SDLK_F11:          return "F11";
        case SDLK_F12:          return "F12";
        default:                break;
    }

    if (key >= SDLK_KP_0 && key <= SDLK_KP_9) {
        buf[0] = (char)('0' + (key - SDLK_KP_0));
        buf[1] = '\0';
        return buf;
    }

    return "Unidentified";
}

/* SDL scancode -> DOM "code" string */

static const char *sdl_to_dom_code(SDL_Scancode sc)
{
    static char buf[16];

    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        snprintf(buf, sizeof(buf), "Key%c", 'A' + (sc - SDL_SCANCODE_A));
        return buf;
    }

    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
        snprintf(buf, sizeof(buf), "Digit%c", '1' + (sc - SDL_SCANCODE_1));
        return buf;
    }
    if (sc == SDL_SCANCODE_0) return "Digit0";

    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) {
        snprintf(buf, sizeof(buf), "Numpad%c", '1' + (sc - SDL_SCANCODE_KP_1));
        return buf;
    }
    if (sc == SDL_SCANCODE_KP_0) return "Numpad0";

    switch (sc) {
        case SDL_SCANCODE_RETURN:       return "Enter";
        case SDL_SCANCODE_KP_ENTER:     return "NumpadEnter";
        case SDL_SCANCODE_ESCAPE:       return "Escape";
        case SDL_SCANCODE_BACKSPACE:    return "Backspace";
        case SDL_SCANCODE_TAB:          return "Tab";
        case SDL_SCANCODE_SPACE:        return "Space";
        case SDL_SCANCODE_LEFT:         return "ArrowLeft";
        case SDL_SCANCODE_UP:           return "ArrowUp";
        case SDL_SCANCODE_RIGHT:        return "ArrowRight";
        case SDL_SCANCODE_DOWN:         return "ArrowDown";
        case SDL_SCANCODE_PAGEUP:       return "PageUp";
        case SDL_SCANCODE_PAGEDOWN:     return "PageDown";
        case SDL_SCANCODE_HOME:         return "Home";
        case SDL_SCANCODE_END:          return "End";
        case SDL_SCANCODE_INSERT:       return "Insert";
        case SDL_SCANCODE_DELETE:       return "Delete";
        case SDL_SCANCODE_LSHIFT:       return "ShiftLeft";
        case SDL_SCANCODE_RSHIFT:       return "ShiftRight";
        case SDL_SCANCODE_LCTRL:        return "ControlLeft";
        case SDL_SCANCODE_RCTRL:        return "ControlRight";
        case SDL_SCANCODE_LALT:         return "AltLeft";
        case SDL_SCANCODE_RALT:         return "AltRight";
        case SDL_SCANCODE_LGUI:         return "MetaLeft";
        case SDL_SCANCODE_RGUI:         return "MetaRight";
        case SDL_SCANCODE_CAPSLOCK:     return "CapsLock";
        case SDL_SCANCODE_F1:           return "F1";
        case SDL_SCANCODE_F2:           return "F2";
        case SDL_SCANCODE_F3:           return "F3";
        case SDL_SCANCODE_F4:           return "F4";
        case SDL_SCANCODE_F5:           return "F5";
        case SDL_SCANCODE_F6:           return "F6";
        case SDL_SCANCODE_F7:           return "F7";
        case SDL_SCANCODE_F8:           return "F8";
        case SDL_SCANCODE_F9:           return "F9";
        case SDL_SCANCODE_F10:          return "F10";
        case SDL_SCANCODE_F11:          return "F11";
        case SDL_SCANCODE_F12:          return "F12";
        case SDL_SCANCODE_MINUS:        return "Minus";
        case SDL_SCANCODE_EQUALS:       return "Equal";
        case SDL_SCANCODE_LEFTBRACKET:  return "BracketLeft";
        case SDL_SCANCODE_RIGHTBRACKET: return "BracketRight";
        case SDL_SCANCODE_BACKSLASH:    return "Backslash";
        case SDL_SCANCODE_SEMICOLON:    return "Semicolon";
        case SDL_SCANCODE_APOSTROPHE:   return "Quote";
        case SDL_SCANCODE_GRAVE:        return "Backquote";
        case SDL_SCANCODE_COMMA:        return "Comma";
        case SDL_SCANCODE_PERIOD:       return "Period";
        case SDL_SCANCODE_SLASH:        return "Slash";
        default:                        return "Unidentified";
    }
}

/* SDL mouse button -> DOM button index */

static int sdl_to_dom_button(uint8_t sdl_button)
{
    switch (sdl_button) {
        case SDL_BUTTON_LEFT:   return 0;
        case SDL_BUTTON_MIDDLE: return 1;
        case SDL_BUTTON_RIGHT:  return 2;
        default:                return (int)sdl_button - 1;
    }
}

/* Event translation */

static void process_key_event(const SDL_KeyboardEvent *kev, InputEventType type)
{
    InputEvent ev;
    memset(&ev, 0, sizeof(ev));

    ev.type = type;
    ev.keyCode = sdl_to_dom_keycode(kev->keysym.sym);
    ev.repeat = (kev->repeat != 0);
    ev.shiftKey = (kev->keysym.mod & KMOD_SHIFT) != 0;
    ev.ctrlKey  = (kev->keysym.mod & KMOD_CTRL)  != 0;
    ev.altKey   = (kev->keysym.mod & KMOD_ALT)   != 0;
    ev.metaKey  = (kev->keysym.mod & KMOD_GUI)   != 0;

    const char *key_str = sdl_to_dom_key(kev->keysym.sym, kev->keysym.mod);
    strncpy(ev.key, key_str, sizeof(ev.key) - 1);

    const char *code_str = sdl_to_dom_code(kev->keysym.scancode);
    strncpy(ev.code, code_str, sizeof(ev.code) - 1);

    input_manager_push_event(&ev);
}

static void process_mouse_button(const SDL_MouseButtonEvent *mev, InputEventType type)
{
    InputEvent ev;
    memset(&ev, 0, sizeof(ev));

    ev.type = type;
    ev.mouseX = mev->x;
    ev.mouseY = mev->y;
    ev.button = sdl_to_dom_button(mev->button);

    input_manager_push_event(&ev);
}

static void process_mouse_motion(const SDL_MouseMotionEvent *mev)
{
    InputEvent ev;
    memset(&ev, 0, sizeof(ev));

    ev.type = INPUT_MOUSE_MOVE;
    ev.mouseX = mev->x;
    ev.mouseY = mev->y;

    input_manager_push_event(&ev);
}

static void process_mouse_wheel(const SDL_MouseWheelEvent *wev)
{
    InputEvent ev;
    memset(&ev, 0, sizeof(ev));

    ev.type = INPUT_WHEEL;

    double dx = (double)wev->x;
    double dy = (double)wev->y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (wev->direction == SDL_MOUSEWHEEL_FLIPPED) {
        dx = -dx;
        dy = -dy;
    }
#else
    (void)wev;
#endif
    /* DOM: positive deltaY = scroll down; SDL: positive y = scroll up. */
    ev.wheelDeltaX = dx;
    ev.wheelDeltaY = -dy;

    /* Wheel events carry no position; attach the current pointer. */
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    ev.mouseX = mx;
    ev.mouseY = my;

    input_manager_push_event(&ev);
}

/* Gamepads */

static int find_controller_slot(Platform *p, SDL_JoystickID id)
{
    for (int i = 0; i < INPUT_MAX_GAMEPADS; i++) {
        if (p->controllers[i] && p->controller_ids[i] == id)
            return i;
    }
    return -1;
}

static int find_free_slot(Platform *p)
{
    for (int i = 0; i < INPUT_MAX_GAMEPADS; i++) {
        if (!p->controllers[i])
            return i;
    }
    return -1;
}

static void open_controller(Platform *p, int device_index)
{
    if (!SDL_IsGameController(device_index)) return;

    /* SDL also sends CONTROLLERDEVICEADDED for devices already open from the
       init loop; skip those. */
    SDL_JoystickID new_id = SDL_JoystickGetDeviceInstanceID(device_index);
    if (new_id >= 0 && find_controller_slot(p, new_id) >= 0) return;

    int slot = find_free_slot(p);
    if (slot < 0) return;

    SDL_GameController *gc = SDL_GameControllerOpen(device_index);
    if (!gc) return;

    SDL_Joystick *joy = SDL_GameControllerGetJoystick(gc);
    SDL_JoystickID id = SDL_JoystickInstanceID(joy);

    p->controllers[slot] = gc;
    p->controller_ids[slot] = id;

    GamepadState state;
    memset(&state, 0, sizeof(state));
    state.connected = true;
    state.index = slot;
    const char *name = SDL_GameControllerName(gc);
    if (name) {
        strncpy(state.id, name, sizeof(state.id) - 1);
    } else {
        strncpy(state.id, "Unknown Controller", sizeof(state.id) - 1);
    }
    input_manager_set_gamepad(slot, &state);
}

static void close_controller(Platform *p, SDL_JoystickID id)
{
    int slot = find_controller_slot(p, id);
    if (slot < 0) return;

    SDL_GameControllerClose(p->controllers[slot]);
    p->controllers[slot] = NULL;
    p->controller_ids[slot] = 0;
    input_manager_clear_gamepad(slot);
}

/* Poll every open controller into the input manager. */
static void update_gamepad_state(Platform *p)
{
    for (int i = 0; i < INPUT_MAX_GAMEPADS; i++) {
        SDL_GameController *gc = p->controllers[i];
        if (!gc) continue;

        GamepadState state;
        memset(&state, 0, sizeof(state));
        state.connected = true;
        state.index = i;
        const char *name = SDL_GameControllerName(gc);
        if (name) {
            strncpy(state.id, name, sizeof(state.id) - 1);
        } else {
            strncpy(state.id, "Unknown Controller", sizeof(state.id) - 1);
        }

        /* W3C Standard Gamepad button order: A B X Y LB RB LT RT Back Start
           LS RS DUp DDown DLeft DRight Guide. */
        static const SDL_GameControllerButton btn_map[] = {
            SDL_CONTROLLER_BUTTON_A,
            SDL_CONTROLLER_BUTTON_B,
            SDL_CONTROLLER_BUTTON_X,
            SDL_CONTROLLER_BUTTON_Y,
            SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
            SDL_CONTROLLER_BUTTON_INVALID,   /* LT: analog, filled in below */
            SDL_CONTROLLER_BUTTON_INVALID,   /* RT: analog, filled in below */
            SDL_CONTROLLER_BUTTON_BACK,
            SDL_CONTROLLER_BUTTON_START,
            SDL_CONTROLLER_BUTTON_LEFTSTICK,
            SDL_CONTROLLER_BUTTON_RIGHTSTICK,
            SDL_CONTROLLER_BUTTON_DPAD_UP,
            SDL_CONTROLLER_BUTTON_DPAD_DOWN,
            SDL_CONTROLLER_BUTTON_DPAD_LEFT,
            SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
            SDL_CONTROLLER_BUTTON_GUIDE,
        };

        for (int b = 0; b < INPUT_GAMEPAD_BUTTONS; b++) {
            if (btn_map[b] != SDL_CONTROLLER_BUTTON_INVALID) {
                bool pressed = SDL_GameControllerGetButton(gc, btn_map[b]) != 0;
                state.buttons[b].pressed = pressed;
                state.buttons[b].value = pressed ? 1.0 : 0.0;
            }
        }

        int16_t lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        int16_t rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        double lt_val = (double)lt / 32767.0;
        double rt_val = (double)rt / 32767.0;
        state.buttons[6].value = lt_val;
        state.buttons[6].pressed = lt_val > 0.5;
        state.buttons[7].value = rt_val;
        state.buttons[7].pressed = rt_val > 0.5;

        /* Axes LX, LY, RX, RY normalized to -1..1. */
        static const SDL_GameControllerAxis axis_map[] = {
            SDL_CONTROLLER_AXIS_LEFTX,
            SDL_CONTROLLER_AXIS_LEFTY,
            SDL_CONTROLLER_AXIS_RIGHTX,
            SDL_CONTROLLER_AXIS_RIGHTY,
        };

        for (int a = 0; a < INPUT_GAMEPAD_AXES; a++) {
            int16_t raw = SDL_GameControllerGetAxis(gc, axis_map[a]);
            state.axes[a] = (double)raw / 32767.0;
        }

        input_manager_set_gamepad(i, &state);
    }
}

/* Lifecycle */

Platform *platform_init(const char *title, int width, int height)
{
    /* Audio and gamecontroller are optional (absent in WSL / CI / headless). */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init(VIDEO) failed: %s\n", SDL_GetError());
        return NULL;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        fprintf(stderr, "Warning: audio init failed: %s\n", SDL_GetError());
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        fprintf(stderr, "Warning: gamecontroller init failed: %s\n", SDL_GetError());


    /* OpenGL 4.5 core: the renderer relies on direct state access. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return NULL;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return NULL;
    }

    /* Windows' opengl32.dll exports only GL 1.1; newer entry points must be
       resolved from the driver at runtime. */
    if (gl_loader_init() != 0) {
        fprintf(stderr, "OpenGL 4.5 entry points unavailable (driver too old?)\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return NULL;
    }

    SDL_GL_SetSwapInterval(1);

    Platform *p = calloc(1, sizeof(Platform));
    if (!p) {
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return NULL;
    }
    p->window = window;
    p->gl_ctx = gl_ctx;

    int num_joysticks = SDL_NumJoysticks();
    for (int i = 0; i < num_joysticks; i++) {
        open_controller(p, i);
    }

    return p;
}

void platform_shutdown(Platform *p)
{
    if (!p) return;

    for (int i = 0; i < INPUT_MAX_GAMEPADS; i++) {
        if (p->controllers[i]) {
            SDL_GameControllerClose(p->controllers[i]);
            p->controllers[i] = NULL;
        }
    }

    if (p->gl_ctx) SDL_GL_DeleteContext(p->gl_ctx);
    if (p->window) SDL_DestroyWindow(p->window);
    SDL_Quit();
    free(p);
}

bool platform_poll_events(Platform *p)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                return false;

            case SDL_KEYDOWN:
                process_key_event(&ev.key, INPUT_KEY_DOWN);
                break;

            case SDL_KEYUP:
                process_key_event(&ev.key, INPUT_KEY_UP);
                break;

            case SDL_MOUSEBUTTONDOWN:
                process_mouse_button(&ev.button, INPUT_MOUSE_DOWN);
                break;

            case SDL_MOUSEBUTTONUP:
                process_mouse_button(&ev.button, INPUT_MOUSE_UP);
                break;

            case SDL_MOUSEMOTION:
                process_mouse_motion(&ev.motion);
                break;

            case SDL_MOUSEWHEEL:
                process_mouse_wheel(&ev.wheel);
                break;

            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                    ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    p->resize_pending = true;
                    p->resize_w = ev.window.data1;
                    p->resize_h = ev.window.data2;
                }
                break;

            case SDL_CONTROLLERDEVICEADDED:
                open_controller(p, ev.cdevice.which);
                break;

            case SDL_CONTROLLERDEVICEREMOVED:
                close_controller(p, ev.cdevice.which);
                break;

            default:
                break;
        }
    }

    update_gamepad_state(p);

    return true;
}

void platform_swap_buffers(Platform *p)
{
    if (!p || !p->window) return;
    SDL_GL_SwapWindow(p->window);
}

void platform_get_window_size(Platform *p, int *w, int *h)
{
    if (!p || !p->window) return;
    SDL_GetWindowSize(p->window, w, h);
}

void platform_set_window_size(Platform *p, int w, int h)
{
    if (!p || !p->window || w <= 0 || h <= 0) return;
    SDL_SetWindowSize(p->window, w, h);
    int display = SDL_GetWindowDisplayIndex(p->window);
    if (display < 0) display = 0;
    SDL_SetWindowPosition(p->window, SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                          SDL_WINDOWPOS_CENTERED_DISPLAY(display));
}

float platform_get_desktop_scale(Platform *p)
{
    if (!p || !p->window) return 1.0f;
    /* Where the drawable is already larger than the window (macOS HiDPI),
       SDL sizes windows in points and the scaling is applied for us. */
    if (platform_get_display_scale(p) > 1.0f) return 1.0f;

    int display = SDL_GetWindowDisplayIndex(p->window);
    if (display < 0) display = 0;
    float ddpi = 0.0f;
    if (SDL_GetDisplayDPI(display, &ddpi, NULL, NULL) != 0 || ddpi <= 0.0f) return 1.0f;
    float scale = ddpi / 96.0f;
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 4.0f) scale = 4.0f;
    return scale;
}

/* Fullscreen */

void platform_set_window_title(Platform *p, const char *title)
{
    if (!p || !p->window) return;
    SDL_SetWindowTitle(p->window, title ? title : "");
}

void platform_set_fullscreen(Platform *p, bool fullscreen)
{
    if (!p || !p->window) return;
    /* Borderless desktop fullscreen avoids a display mode switch. */
    Uint32 flag = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    SDL_SetWindowFullscreen(p->window, flag);
}

bool platform_is_fullscreen(Platform *p)
{
    if (!p || !p->window) return false;
    Uint32 flags = SDL_GetWindowFlags(p->window);
    return (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
}

/* Display information */

void platform_get_display_size(Platform *p, int *w, int *h)
{
    int display_index = 0;
    if (p && p->window) {
        display_index = SDL_GetWindowDisplayIndex(p->window);
        if (display_index < 0) display_index = 0;
    }

    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(display_index, &mode) == 0) {
        if (w) *w = mode.w;
        if (h) *h = mode.h;
    } else {
        if (w) *w = 1920;
        if (h) *h = 1080;
    }
}

float platform_get_display_scale(Platform *p)
{
    int display_index = 0;
    if (p && p->window) {
        display_index = SDL_GetWindowDisplayIndex(p->window);
        if (display_index < 0) display_index = 0;
    }

    /* SDL_GetDisplayDPI is not available everywhere; the drawable/window
       size ratio is a reliable HiDPI proxy. */
    if (p && p->window) {
        int win_w, gl_w;
        SDL_GetWindowSize(p->window, &win_w, NULL);
        SDL_GL_GetDrawableSize(p->window, &gl_w, NULL);
        if (win_w > 0) {
            float scale = (float)gl_w / (float)win_w;
            if (scale >= 1.0f) return scale;
        }
    }
    return 1.0f;
}

bool platform_check_resize(Platform *p, int *w, int *h)
{
    if (!p || !p->resize_pending) return false;
    p->resize_pending = false;
    if (w) *w = p->resize_w;
    if (h) *h = p->resize_h;
    return true;
}
