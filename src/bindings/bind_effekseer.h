/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_EFFEKSEER_H
#define RMMZ_BIND_EFFEKSEER_H

#include <quickjs.h>

/* Register the __native_effekseer global object exposing Effekseer effect
   loading, playback and drawing to JS. */
void bind_effekseer_register(JSContext *ctx);

#endif /* RMMZ_BIND_EFFEKSEER_H */
