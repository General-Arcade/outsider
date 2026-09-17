/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_IMAGE_H
#define RMMZ_BIND_IMAGE_H

#include <quickjs.h>

/* Register the __native_image global object exposing image loading and the
   image cache to JS. */
void bind_image_register(JSContext *ctx);

#endif /* RMMZ_BIND_IMAGE_H */
