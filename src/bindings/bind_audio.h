/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_AUDIO_H
#define RMMZ_BIND_AUDIO_H

#include <quickjs.h>

/* Register the __native_audio global object exposing the audio engine
   (sources, voices, buses) to JS. */
void bind_audio_register(JSContext *ctx);

#endif /* RMMZ_BIND_AUDIO_H */
