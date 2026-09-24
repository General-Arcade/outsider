/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "platform/android_support.h"

#ifdef __ANDROID__

#include "io/file_io.h"

#include <SDL3/SDL.h>
#include <android/log.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LOG_TAG "outsider"

/* Reads the pipe that stdout/stderr were redirected into and forwards each
   line to logcat. */
static void *logcat_pump(void *arg)
{
    int fd = (int)(intptr_t)arg;
    char buf[1024];
    size_t len = 0;
    for (;;) {
        ssize_t n = read(fd, buf + len, sizeof(buf) - 1 - len);
        if (n <= 0) break;
        len += (size_t)n;
        buf[len] = '\0';
        char *start = buf;
        char *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = '\0';
            if (*start) __android_log_write(ANDROID_LOG_INFO, LOG_TAG, start);
            start = nl + 1;
        }
        len = strlen(start);
        if (len == sizeof(buf) - 1) {
            /* Line longer than the buffer: flush it as-is. */
            __android_log_write(ANDROID_LOG_INFO, LOG_TAG, start);
            len = 0;
        } else {
            memmove(buf, start, len + 1);
        }
    }
    return NULL;
}

void android_support_init_logging(void)
{
    int fds[2];
    if (pipe(fds) != 0) return;
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    pthread_t thread;
    if (pthread_create(&thread, NULL, logcat_pump, (void *)(intptr_t)fds[0]) == 0) {
        pthread_detach(thread);
    }
}

bool android_support_files_dir(char *out, size_t out_size)
{
    const char *dir = NULL;
    if (SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) {
        dir = SDL_GetAndroidExternalStoragePath();
    }
    if (!dir) dir = SDL_GetAndroidInternalStoragePath();
    if (!dir || !dir[0]) return false;

    int n = snprintf(out, out_size, "%s", dir);
    if (n <= 0 || (size_t)n >= out_size) return false;
    size_t len = (size_t)n;
    while (len > 1 && out[len - 1] == '/') out[--len] = '\0';
    return true;
}

bool android_support_extract_shims(const char *dest_dir, const char *const *names)
{
    if (!dest_dir || !names) return false;

    char shims_dir[1024];
    snprintf(shims_dir, sizeof(shims_dir), "%s/shims", dest_dir);
    if (!file_io_mkdir(shims_dir)) return false;

    bool all_ok = true;
    for (int i = 0; names[i] != NULL; i++) {
        /* SDL_LoadFile reads from the APK's assets on Android. */
        char asset[256];
        snprintf(asset, sizeof(asset), "shims/%s", names[i]);
        size_t size = 0;
        void *data = SDL_LoadFile(asset, &size);
        if (!data) {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                                "missing asset %s: %s", asset, SDL_GetError());
            all_ok = false;
            continue;
        }
        char dest[1024];
        snprintf(dest, sizeof(dest), "%s/%s", shims_dir, names[i]);
        if (!file_io_write_binary(dest, data, size)) all_ok = false;
        SDL_free(data);
    }
    return all_ok;
}

#else /* !__ANDROID__ */

void android_support_init_logging(void)
{
}

bool android_support_files_dir(char *out, size_t out_size)
{
    (void)out;
    (void)out_size;
    return false;
}

bool android_support_extract_shims(const char *dest_dir, const char *const *names)
{
    (void)dest_dir;
    (void)names;
    return false;
}

#endif
