/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_ANDROID_SUPPORT_H
#define RMMZ_ANDROID_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>

/* Android has no stdout: route stdout/stderr into logcat (tag "outsider").
   No-op on other platforms. */
void android_support_init_logging(void);

/* App-private external files directory (where the game is expected), with
   forward slashes and no trailing separator. False when unavailable or not
   on Android. */
bool android_support_files_dir(char *out, size_t out_size);

/* Unpack the shim scripts from the APK's assets into <dest_dir>/shims.
   `names` is a NULL-terminated list of shim file names. Returns true when
   every shim was written. False (and nothing done) off Android. */
bool android_support_extract_shims(const char *dest_dir, const char *const *names);

#endif /* RMMZ_ANDROID_SUPPORT_H */
