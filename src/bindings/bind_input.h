/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_INPUT_H
#define RMMZ_BIND_INPUT_H

#include <quickjs.h>

/* Register the __native_input global object exposing event polling and
   gamepad state to JS. */
void bind_input_register(JSContext *ctx);

#endif /* RMMZ_BIND_INPUT_H */
