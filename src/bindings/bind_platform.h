/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_PLATFORM_H
#define RMMZ_BIND_PLATFORM_H

#include <quickjs.h>

typedef struct Platform Platform;

/* Register __native_platform bindings in the given JS context.
   Must be called after platform_init(). */
void bind_platform_register(JSContext *ctx, Platform *platform);

#endif /* RMMZ_BIND_PLATFORM_H */
