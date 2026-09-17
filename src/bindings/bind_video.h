/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_VIDEO_H
#define RMMZ_BIND_VIDEO_H

#include <quickjs.h>

/* Register the __native_video global object (MPEG-1 playback for the
   HTMLVideoElement shim). */
void bind_video_register(JSContext *ctx);

#endif /* RMMZ_BIND_VIDEO_H */
